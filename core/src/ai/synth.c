/*
 * Hybrid symbolic code synthesizer:
 *   1. Extracts target ("bml" | "lua") and slots (name, speed, flag, etc.)
 *   2. Maps assist.c entry_id / keywords to a Lisp goal S-expression
 *   3. Evaluates the goal in bm_lisp (AST composition + micro-solver)
 *   4. Emits verified .b (BML) or Lua 5.4 code
 */
#include "synth.h"
#include "lisp.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static uint8_t synth_arena[BM_LISP_DEFAULT_ARENA];
static bm_lisp_t *synth_vm;
static int synth_ready;

/* Core Lisp helpers + AST-to-(BML|Lua) emitter */
static const char *LISP_EMITTER =
    "(define (nl) \"\n\")\n"
    "(define (join-with sep lst)\n"
    "  (cond ((null? lst) \"\")\n"
    "        ((null? (cdr lst)) (str-cat (car lst)))\n"
    "        (else (str-cat (car lst) sep (join-with sep (cdr lst))))))\n"
    "(define (emit-fn-name tgt fn)\n"
    "  (if (eq? tgt 'lua)\n"
    "      (cond ((eq? fn 'abs) \"math.abs\")\n"
    "            ((eq? fn 'min) \"math.min\")\n"
    "            ((eq? fn 'max) \"math.max\")\n"
    "            (else fn))\n"
    "      fn))\n"
    "(define (emit-expr tgt e)\n"
    "  (match e\n"
    "    ((binop ?op ?a ?b) (str-cat \"(\" (emit-expr tgt ?a) \" \" ?op \" \" (emit-expr tgt ?b) \")\"))\n"
    "    ((unop ?op ?a) (str-cat ?op \" \" (emit-expr tgt ?a)))\n"
    "    ((aref ?arr ?idx) (str-cat ?arr \"[\" (emit-expr tgt ?idx) \"]\"))\n"
    "    ((call ?fn . ?args) (str-cat (emit-fn-name tgt ?fn) \"(\" (join-with \", \" (map (lambda (x) (emit-expr tgt x)) ?args)) \")\"))\n"
    "    (_ (str-cat e))))\n"
    "(define (emit-params tgt ps)\n"
    "  (join-with \", \"\n"
    "    (map (lambda (p)\n"
    "           (if (eq? tgt 'bml)\n"
    "               (str-cat (car p) \": \" (car (cdr p)))\n"
    "               (str-cat (car p))))\n"
    "         ps)))\n"
    "(define (emit-cond-rest tgt ind clauses)\n"
    "  (if (null? clauses)\n"
    "      (str-cat ind \"end\" (nl))\n"
    "      (let ((cl (car clauses)))\n"
    "        (if (eq? (car cl) 'else)\n"
    "            (str-cat ind \"else\" (nl)\n"
    "                     (emit-block tgt (str-cat ind \"  \") (car (cdr cl)))\n"
    "                     ind \"end\" (nl))\n"
    "            (str-cat ind \"elseif \" (emit-expr tgt (car cl)) \" then\" (nl)\n"
    "                     (emit-block tgt (str-cat ind \"  \") (car (cdr cl)))\n"
    "                     (emit-cond-rest tgt ind (cdr clauses)))))))\n"
    "(define (emit-stmt tgt ind s)\n"
    "  (match s\n"
    "    ((var ?n ?t ?v)\n"
    "     (if (eq? tgt 'bml)\n"
    "         (str-cat ind \"local \" ?n \": \" ?t \" = \" (emit-expr tgt ?v) (nl))\n"
    "         (str-cat ind \"local \" ?n \" = \" (emit-expr tgt ?v) (nl))))\n"
    "    ((set ?n ?v) (str-cat ind ?n \" = \" (emit-expr tgt ?v) (nl)))\n"
    "    ((aset ?arr ?idx ?v) (str-cat ind ?arr \"[\" (emit-expr tgt ?idx) \"] = \" (emit-expr tgt ?v) (nl)))\n"
    "    ((return ?v) (str-cat ind \"return \" (emit-expr tgt ?v) (nl)))\n"
    "    ((break) (str-cat ind \"break\" (nl)))\n"
    "    ((expr-stmt ?e) (str-cat ind (emit-expr tgt ?e) (nl)))\n"
    "    ((if ?c ?t)\n"
    "     (str-cat ind \"if \" (emit-expr tgt ?c) \" then\" (nl)\n"
    "              (emit-block tgt (str-cat ind \"  \") ?t)\n"
    "              ind \"end\" (nl)))\n"
    "    ((if ?c ?t ?el)\n"
    "     (str-cat ind \"if \" (emit-expr tgt ?c) \" then\" (nl)\n"
    "              (emit-block tgt (str-cat ind \"  \") ?t)\n"
    "              ind \"else\" (nl)\n"
    "              (emit-block tgt (str-cat ind \"  \") ?el)\n"
    "              ind \"end\" (nl)))\n"
    "    ((cond ?first . ?rest)\n"
    "     (str-cat ind \"if \" (emit-expr tgt (car ?first)) \" then\" (nl)\n"
    "              (emit-block tgt (str-cat ind \"  \") (car (cdr ?first)))\n"
    "              (emit-cond-rest tgt ind ?rest)))\n"
    "    ((for ?v ?a ?b ?body)\n"
    "     (str-cat ind \"for \" ?v \" = \" (emit-expr tgt ?a) \", \" (emit-expr tgt ?b) \" do\" (nl)\n"
    "              (emit-block tgt (str-cat ind \"  \") ?body)\n"
    "              ind \"end\" (nl)))\n"
    "    ((for-step ?v ?a ?b ?step ?body)\n"
    "     (str-cat ind \"for \" ?v \" = \" (emit-expr tgt ?a) \", \" (emit-expr tgt ?b) \", \" (emit-expr tgt ?step) \" do\" (nl)\n"
    "              (emit-block tgt (str-cat ind \"  \") ?body)\n"
    "              ind \"end\" (nl)))\n"
    "    ((while ?c ?body)\n"
    "     (str-cat ind \"while \" (emit-expr tgt ?c) \" do\" (nl)\n"
    "              (emit-block tgt (str-cat ind \"  \") ?body)\n"
    "              ind \"end\" (nl)))\n"
    "    (_ \"\")))\n"
    "(define (emit-block tgt ind stmts)\n"
    "  (if (null? stmts) \"\"\n"
    "      (str-cat (emit-stmt tgt ind (car stmts))\n"
    "               (emit-block tgt ind (cdr stmts)))))\n"
    "(define (emit-ast tgt ast)\n"
    "  (match ast\n"
    "    ((fn ?name ?params ?ret . ?body)\n"
    "     (let ((ret-ann (if (and (eq? tgt 'bml) (not (eq? ?ret 'void)))\n"
    "                        (str-cat \": \" ?ret) \"\")))\n"
    "       (str-cat \"function \" ?name \"(\" (emit-params tgt ?params) \")\" ret-ann (nl)\n"
    "                (emit-block tgt \"  \" ?body)\n"
    "                \"end\")))\n"
    "    ((seq . ?fns) (join-with (str-cat (nl) (nl)) (map (lambda (f) (emit-ast tgt f)) ?fns)))\n"
    "    (_ (if (string? ast) ast (emit-stmt tgt \"\" ast)))))\n";

/* Composable bm domain patterns -> Unified AST */
static const char *LISP_PATTERNS =
    "(define (sym-join a b) (str->sym (str-cat a \"_\" b)))\n"
    "(define (wire-part p)\n"
    "  (match p\n"
    "    ((rect-actor ?n ?c ?w ?h)\n"
    "     (list (str-cat \"  \" ?n \" = { x = 40, y = 100, w = \" ?w \", h = \" ?h \", vx = 0, vy = 0, on_ground = false, alive = true, col = \" ?c \" }\")\n"
    "           \"\"\n"
    "           (str-cat \"  if \" ?n \".alive then rectfill(\" ?n \".x, \" ?n \".y, \" ?n \".x + \" ?n \".w, \" ?n \".y + \" ?n \".h, \" ?n \".col) end\")))\n"
    "    ((accel-x ?n ?acc ?fric ?maxv)\n"
    "     (list \"\"\n"
    "           (str-cat \"  if btn(0) then \" ?n \".vx = math.max(\" ?n \".vx - \" ?acc \", -\" ?maxv \")\" (nl)\n"
    "                    \"  elseif btn(1) then \" ?n \".vx = math.min(\" ?n \".vx + \" ?acc \", \" ?maxv \")\" (nl)\n"
    "                    \"  else\" (nl)\n"
    "                    \"    if \" ?n \".vx > 0 then \" ?n \".vx = math.max(0, \" ?n \".vx - \" ?fric \")\" (nl)\n"
    "                    \"    elseif \" ?n \".vx < 0 then \" ?n \".vx = math.min(0, \" ?n \".vx + \" ?fric \") end\" (nl)\n"
    "                    \"  end\" (nl)\n"
    "                    \"  \" ?n \".x = math.max(0, math.min(320 - \" ?n \".w, \" ?n \".x + \" ?n \".vx))\")\n"
    "           \"\"))\n"
    "    ((gravity-floor ?n ?g ?jv ?b ?fy)\n"
    "     (list \"\"\n"
    "           (str-cat \"  if \" ?n \".on_ground and btnp(\" ?b \") then \" ?n \".vy = -\" ?jv \"; \" ?n \".on_ground = false end\" (nl)\n"
    "                    \"  \" ?n \".vy = math.min(\" ?n \".vy + \" ?g \", 10)\" (nl)\n"
    "                    \"  \" ?n \".y = \" ?n \".y + \" ?n \".vy\" (nl)\n"
    "                    \"  if \" ?n \".y + \" ?n \".h >= \" ?fy \" then \" ?n \".y = \" ?fy \" - \" ?n \".h; \" ?n \".vy = 0; \" ?n \".on_ground = true else \" ?n \".on_ground = false end\")\n"
    "           (str-cat \"  if \" ?fy \" < 180 then rectfill(0, \" ?fy \", 319, 179, 4) end\")))\n"
    "    ((patrol-bounds ?n ?spd ?mn ?mx ?c)\n"
    "     (list (str-cat \"  \" ?n \" = { x = \" (+ ?mn 10) \", y = 132, w = 12, h = 12, dir = 1, alive = true, col = \" ?c \" }\")\n"
    "           (str-cat \"  if \" ?n \".alive then\" (nl)\n"
    "                    \"    \" ?n \".x = \" ?n \".x + \" ?n \".dir * \" ?spd (nl)\n"
    "                    \"    if \" ?n \".x <= \" ?mn \" then \" ?n \".x = \" ?mn \"; \" ?n \".dir = 1 end\" (nl)\n"
    "                    \"    if \" ?n \".x + \" ?n \".w >= \" ?mx \" then \" ?n \".x = \" ?mx \" - \" ?n \".w; \" ?n \".dir = -1 end\" (nl)\n"
    "                    \"  end\")\n"
    "           (str-cat \"  if \" ?n \".alive then rectfill(\" ?n \".x, \" ?n \".y, \" ?n \".x + \" ?n \".w, \" ?n \".y + \" ?n \".h, \" ?n \".col) end\")))\n"
    "    ((stomp-or-hit ?a ?e)\n"
    "     (list \"  score = 0; lives = 3\"\n"
    "           (str-cat \"  if \" ?e \".alive and \" ?a \".alive then\" (nl)\n"
    "                    \"    if math.abs((\" ?a \".x + \" ?a \".w * 0.5) - (\" ?e \".x + \" ?e \".w * 0.5)) < 12 and math.abs((\" ?a \".y + \" ?a \".h * 0.5) - (\" ?e \".y + \" ?e \".h * 0.5)) < 12 then\" (nl)\n"
    "                    \"      if \" ?a \".vy > 0 and (\" ?a \".y + \" ?a \".h) <= (\" ?e \".y + 8) then \" ?e \".alive = false; \" ?a \".vy = -4; score = score + 100\" (nl)\n"
    "                    \"      else lives = math.max(0, lives - 1) end\" (nl)\n"
    "                    \"    end\" (nl)\n"
    "                    \"  end\")\n"
    "           \"  print('SCORE:' .. score .. ' LIVES:' .. lives, 4, 4, 7)\"))\n"
    "    ((bounce-actor ?n ?c ?w ?h ?spd)\n"
    "     (list (str-cat \"  \" ?n \" = { x = 160, y = 80, w = \" ?w \", h = \" ?h \", vx = \" ?spd \", vy = \" ?spd \", alive = true, col = \" ?c \" }\")\n"
    "           (str-cat \"  \" ?n \".x = \" ?n \".x + \" ?n \".vx; \" ?n \".y = \" ?n \".y + \" ?n \".vy\" (nl)\n"
    "                    \"  if \" ?n \".x < 0 or \" ?n \".x + \" ?n \".w > 320 then \" ?n \".vx = -\" ?n \".vx end\" (nl)\n"
    "                    \"  if \" ?n \".y < 0 or \" ?n \".y + \" ?n \".h > 180 then \" ?n \".vy = -\" ?n \".vy end\")\n"
    "           (str-cat \"  rectfill(\" ?n \".x, \" ?n \".y, \" ?n \".x + \" ?n \".w, \" ?n \".y + \" ?n \".h, \" ?n \".col)\")))\n"
    "    ((collide-bounce ?b ?p)\n"
    "     (list \"\"\n"
    "           (str-cat \"  if \" ?b \".vy > 0 and \" ?b \".x + \" ?b \".w >= \" ?p \".x and \" ?b \".x <= \" ?p \".x + \" ?p \".w and \" ?b \".y + \" ?b \".h >= \" ?p \".y and \" ?b \".y <= \" ?p \".y + \" ?p \".h then\" (nl)\n"
    "                    \"    \" ?b \".y = \" ?p \".y - \" ?b \".h; \" ?b \".vy = -\" ?b \".vy\" (nl)\n"
    "                    \"  end\")\n"
    "           \"\"))\n"
    "    ((moving-platform ?n ?spd ?mn ?mx ?w ?h ?c)\n"
    "     (list (str-cat \"  \" ?n \" = { x = \" (+ ?mn 10) \", y = 120, w = \" ?w \", h = \" ?h \", dir = 1, spd = \" ?spd \", col = \" ?c \" }\")\n"
    "           (str-cat \"  \" ?n \".x = \" ?n \".x + \" ?n \".dir * \" ?spd (nl)\n"
    "                    \"  if \" ?n \".x <= \" ?mn \" then \" ?n \".x = \" ?mn \"; \" ?n \".dir = 1 end\" (nl)\n"
    "                    \"  if \" ?n \".x + \" ?n \".w >= \" ?mx \" then \" ?n \".x = \" ?mx \" - \" ?n \".w; \" ?n \".dir = -1 end\")\n"
    "           (str-cat \"  rectfill(\" ?n \".x, \" ?n \".y, \" ?n \".x + \" ?n \".w, \" ?n \".y + \" ?n \".h, \" ?n \".col)\")))\n"
    "    ((land-on ?a ?p)\n"
    "     (list \"\"\n"
    "           (str-cat \"  if \" ?a \".vy >= 0 and \" ?a \".x + \" ?a \".w > \" ?p \".x and \" ?a \".x < \" ?p \".x + \" ?p \".w and \" ?a \".y + \" ?a \".h >= \" ?p \".y and \" ?a \".y + \" ?a \".h <= \" ?p \".y + 10 then\" (nl)\n"
    "                    \"    \" ?a \".y = \" ?p \".y - \" ?a \".h; \" ?a \".vy = 0; \" ?a \".on_ground = true; \" ?a \".x = \" ?a \".x + \" ?p \".dir * (\" ?p \".spd or 0)\" (nl)\n"
    "                    \"  end\")\n"
    "           \"\"))\n"
    "    ((shmup-core ?spd ?cd)\n"
    "     (list (str-cat \"  ship = { x = 152, y = 150, w = 14, h = 10, cd = 0 }; shots = {}; enemies = {}; score = 0\" (nl)\n"
    "                    \"  for i = 1, 5 do enemies[i] = { x = i * 50, y = 24, w = 12, h = 10, dir = 1, alive = true } end\")\n"
    "           (str-cat \"  if btn(0) then ship.x = math.max(4, ship.x - \" ?spd \") end\" (nl)\n"
    "                    \"  if btn(1) then ship.x = math.min(302, ship.x + \" ?spd \") end\" (nl)\n"
    "                    \"  if ship.cd > 0 then ship.cd = ship.cd - 1 elseif btn(4) then ship.cd = \" ?cd \"; shots[#shots + 1] = { x = ship.x + 6, y = ship.y - 4, alive = true } end\" (nl)\n"
    "                    \"  for _, s in ipairs(shots) do if s.alive then s.y = s.y - 4; if s.y < 0 then s.alive = false end end end\" (nl)\n"
    "                    \"  for _, e in ipairs(enemies) do if e.alive then e.x = e.x + e.dir; if e.x < 8 or e.x > 300 then e.dir = -e.dir end end end\")\n"
    "           (str-cat \"  rectfill(ship.x, ship.y, ship.x + ship.w, ship.y + ship.h, 11)\" (nl)\n"
    "                    \"  for _, s in ipairs(shots) do if s.alive then rectfill(s.x, s.y, s.x + 2, s.y + 5, 10) end end\" (nl)\n"
    "                    \"  for _, e in ipairs(enemies) do if e.alive then rectfill(e.x, e.y, e.x + e.w, e.y + e.h, 8) end end\" (nl)\n"
    "                    \"  print('SCORE:' .. score, 4, 4, 7)\")))\n"
    "    ((breakout-core ?spd)\n"
    "     (list (str-cat \"  pad = { x = 136, y = 164, w = 48, h = 6 }; ball = { x = 160, y = 120, vx = 2, vy = -2 }; bricks = {}; score = 0\" (nl)\n"
    "                    \"  for i = 1, 8 do bricks[i] = { x = 16 + (i - 1) * 36, y = 28, w = 32, h = 8, alive = true } end\")\n"
    "           (str-cat \"  if btn(0) then pad.x = math.max(0, pad.x - \" ?spd \") end\" (nl)\n"
    "                    \"  if btn(1) then pad.x = math.min(320 - pad.w, pad.x + \" ?spd \") end\" (nl)\n"
    "                    \"  ball.x = ball.x + ball.vx; ball.y = ball.y + ball.vy\" (nl)\n"
    "                    \"  if ball.x < 4 or ball.x > 316 then ball.vx = -ball.vx end\" (nl)\n"
    "                    \"  if ball.y < 4 then ball.vy = -ball.vy end\" (nl)\n"
    "                    \"  if ball.vy > 0 and ball.y >= pad.y - 4 and ball.x >= pad.x and ball.x <= pad.x + pad.w then ball.vy = -ball.vy end\" (nl)\n"
    "                    \"  for _, b in ipairs(bricks) do if b.alive and ball.x >= b.x and ball.x <= b.x + b.w and ball.y >= b.y and ball.y <= b.y + b.h then b.alive = false; ball.vy = -ball.vy; score = score + 10 end end\")\n"
    "           (str-cat \"  rectfill(pad.x, pad.y, pad.x + pad.w, pad.y + pad.h, 7)\" (nl)\n"
    "                    \"  circfill(ball.x, ball.y, 4, 10)\" (nl)\n"
    "                    \"  for _, b in ipairs(bricks) do if b.alive then rectfill(b.x, b.y, b.x + b.w, b.y + b.h, 9) end end\" (nl)\n"
    "                    \"  print('SCORE:' .. score, 4, 4, 7)\")))\n"
    "    ((topdown-core ?spd)\n"
    "     (list (str-cat \"  state = 'play'; score = 0; hp = 3; ifr = 0\" (nl)\n"
    "                    \"  hero = { x = 152, y = 120, w = 12, h = 12, dx = 0, dy = -1 }\" (nl)\n"
    "                    \"  wall = { x = 112, y = 68, w = 96, h = 14 }\" (nl)\n"
    "                    \"  gems = { {x=40,y=36,got=false}, {x=270,y=36,got=false}, {x=270,y=144,got=false} }\" (nl)\n"
    "                    \"  foe = { x = 60, y = 40, w = 12, h = 12, dir = 1, alive = true }\")\n"
    "           (str-cat \"  if state ~= 'play' then if btnp(4) then _init() end; return end\" (nl)\n"
    "                    \"  if ifr > 0 then ifr = ifr - 1 end\" (nl)\n"
    "                    \"  local nx, ny = hero.x, hero.y\" (nl)\n"
    "                    \"  if btn(0) then nx = math.max(4, nx - \" ?spd \"); hero.dx, hero.dy = -1, 0 end\" (nl)\n"
    "                    \"  if btn(1) then nx = math.min(304, nx + \" ?spd \"); hero.dx, hero.dy = 1, 0 end\" (nl)\n"
    "                    \"  if btn(2) then ny = math.max(16, ny - \" ?spd \"); hero.dx, hero.dy = 0, -1 end\" (nl)\n"
    "                    \"  if btn(3) then ny = math.min(164, ny + \" ?spd \"); hero.dx, hero.dy = 0, 1 end\" (nl)\n"
    "                    \"  if nx + hero.w < wall.x or nx > wall.x + wall.w or ny + hero.h < wall.y or ny > wall.y + wall.h then hero.x, hero.y = nx, ny end\" (nl)\n"
    "                    \"  if foe.alive then\" (nl)\n"
    "                    \"    foe.x = foe.x + foe.dir; if foe.x < 16 or foe.x > 290 then foe.dir = -foe.dir end\" (nl)\n"
    "                    \"    if ifr == 0 and math.abs(hero.x - foe.x) < 11 and math.abs(hero.y - foe.y) < 11 then\" (nl)\n"
    "                    \"      hp = hp - 1; ifr = 40; if hp <= 0 then state = 'gameover' end\" (nl)\n"
    "                    \"    end\" (nl)\n"
    "                    \"    if btnp(4) and math.abs((hero.x + hero.dx * 14) - foe.x) < 16 and math.abs((hero.y + hero.dy * 14) - foe.y) < 16 then\" (nl)\n"
    "                    \"      foe.alive = false; score = score + 100\" (nl)\n"
    "                    \"    end\" (nl)\n"
    "                    \"  end\" (nl)\n"
    "                    \"  local left = 0\" (nl)\n"
    "                    \"  for _, g in ipairs(gems) do if not g.got then\" (nl)\n"
    "                    \"    if math.abs(hero.x + 6 - g.x) < 12 and math.abs(hero.y + 6 - g.y) < 12 then g.got = true; score = score + 50 else left = left + 1 end\" (nl)\n"
    "                    \"  end end\" (nl)\n"
    "                    \"  if left == 0 then state = 'win' end\")\n"
    "           (str-cat \"  rectfill(wall.x, wall.y, wall.x + wall.w, wall.y + wall.h, 5)\" (nl)\n"
    "                    \"  for _, g in ipairs(gems) do if not g.got then circfill(g.x, g.y, 4, 10) end end\" (nl)\n"
    "                    \"  if foe.alive then rectfill(foe.x, foe.y, foe.x + foe.w, foe.y + foe.h, 8) end\" (nl)\n"
    "                    \"  if ifr % 4 < 2 then rectfill(hero.x, hero.y, hero.x + hero.w, hero.y + hero.h, 11) end\" (nl)\n"
    "                    \"  print('HP:' .. hp .. ' SCORE:' .. score, 4, 4, 7)\" (nl)\n"
    "                    \"  if state ~= 'play' then print(state == 'win' and 'YOU WIN! (A)' or 'GAME OVER (A)', 104, 88, 10) end\")))\n"
    "    ((runner-core ?spd ?grav ?jv)\n"
    "     (list \"  runner = { x = 40, y = 130, w = 12, h = 14, vy = 0, on_ground = true }; obs = { x = 300, y = 132, w = 12, h = 12 }; score = 0\"\n"
    "           (str-cat \"  if runner.on_ground and btnp(4) then runner.vy = -\" ?jv \"; runner.on_ground = false end\" (nl)\n"
    "                    \"  runner.vy = math.min(runner.vy + \" ?grav \", 8); runner.y = runner.y + runner.vy\" (nl)\n"
    "                    \"  if runner.y >= 130 then runner.y = 130; runner.vy = 0; runner.on_ground = true end\" (nl)\n"
    "                    \"  obs.x = obs.x - \" ?spd \"; if obs.x < -16 then obs.x = 320; score = score + 10 end\")\n"
    "           (str-cat \"  rectfill(0, 144, 319, 179, 4)\" (nl)\n"
    "                    \"  rectfill(obs.x, obs.y, obs.x + obs.w, obs.y + obs.h, 8)\" (nl)\n"
    "                    \"  rectfill(runner.x, runner.y, runner.x + runner.w, runner.y + runner.h, 11)\" (nl)\n"
    "                    \"  print('SCORE:' .. score, 4, 4, 7)\")))\n"
    "    ((grid-cursor-core ?cols ?rows)\n"
    "     (list (str-cat \"  state = 'play'; score = 0; turns = 20\" (nl)\n"
    "                    \"  cur = { x = 1, y = 1, cols = \" ?cols \", rows = \" ?rows \" }\" (nl)\n"
    "                    \"  goal = { x = \" (- ?cols 2) \", y = \" (- ?rows 2) \" }\" (nl)\n"
    "                    \"  foe = { x = 4, y = 3 }; grid = {}\" (nl)\n"
    "                    \"  for gx = 0, cur.cols - 1 do grid[gx] = {}; for gy = 0, cur.rows - 1 do grid[gx][gy] = 0 end end\" (nl)\n"
    "                    \"  grid[3][2] = 1; grid[5][4] = 1\")\n"
    "           (str-cat \"  if state ~= 'play' then if btnp(4) then _init() end; return end\" (nl)\n"
    "                    \"  local nx, ny = cur.x, cur.y\" (nl)\n"
    "                    \"  if btnp(0) and cur.x > 0 then nx = cur.x - 1 end\" (nl)\n"
    "                    \"  if btnp(1) and cur.x < cur.cols - 1 then nx = cur.x + 1 end\" (nl)\n"
    "                    \"  if btnp(2) and cur.y > 0 then ny = cur.y - 1 end\" (nl)\n"
    "                    \"  if btnp(3) and cur.y < cur.rows - 1 then ny = cur.y + 1 end\" (nl)\n"
    "                    \"  if (nx ~= cur.x or ny ~= cur.y) and grid[nx][ny] == 0 then\" (nl)\n"
    "                    \"    cur.x, cur.y = nx, ny; turns = turns - 1\" (nl)\n"
    "                    \"    if cur.x < foe.x then foe.x = foe.x - 1 elseif cur.x > foe.x then foe.x = foe.x + 1 end\" (nl)\n"
    "                    \"    if cur.x == goal.x and cur.y == goal.y then score = score + turns * 20 + 100; state = 'win'\" (nl)\n"
    "                    \"    elseif (cur.x == foe.x and cur.y == foe.y) or turns <= 0 then state = 'gameover' end\" (nl)\n"
    "                    \"  end\" (nl)\n"
    "                    \"  if btnp(4) then score = score + 5 end\")\n"
    "           (str-cat \"  for gx = 0, cur.cols - 1 do for gy = 0, cur.rows - 1 do\" (nl)\n"
    "                    \"    local sx, sy = 40 + gx * 22, 20 + gy * 20\" (nl)\n"
    "                    \"    if grid[gx][gy] == 1 then rectfill(sx, sy, sx + 18, sy + 18, 5) else rect(sx, sy, sx + 18, sy + 18, 1) end\" (nl)\n"
    "                    \"  end end\" (nl)\n"
    "                    \"  rectfill(42 + goal.x * 22, 22 + goal.y * 20, 56 + goal.x * 22, 36 + goal.y * 20, 11)\" (nl)\n"
    "                    \"  rectfill(42 + foe.x * 22, 22 + foe.y * 20, 56 + foe.x * 22, 36 + foe.y * 20, 8)\" (nl)\n"
    "                    \"  rectfill(42 + cur.x * 22, 22 + cur.y * 20, 56 + cur.x * 22, 36 + cur.y * 20, 10)\" (nl)\n"
    "                    \"  print('TURNS:' .. turns .. ' SCORE:' .. score, 4, 4, 7)\" (nl)\n"
    "                    \"  if state ~= 'play' then print(state == 'win' and 'CLEAR! (A)' or 'GAME OVER (A)', 110, 88, 10) end\")))\n"
    "    ((match-puzzle-core ?spd)\n"
    "     (list (str-cat \"  state = 'play'; score = 0; board = {}\" (nl)\n"
    "                    \"  for y = 0, 13 do board[y] = {}; for x = 0, 9 do board[y][x] = 0 end end\" (nl)\n"
    "                    \"  piece = { x = 4, y = 0, col = 8, t = 0 }\")\n"
    "           (str-cat \"  if state ~= 'play' then if btnp(4) then _init() end; return end\" (nl)\n"
    "                    \"  if btnp(0) and piece.x > 0 and board[piece.y][piece.x - 1] == 0 then piece.x = piece.x - 1 end\" (nl)\n"
    "                    \"  if btnp(1) and piece.x < 9 and board[piece.y][piece.x + 1] == 0 then piece.x = piece.x + 1 end\" (nl)\n"
    "                    \"  if btnp(4) then piece.col = 8 + ((piece.col - 7) % 4) end\" (nl)\n"
    "                    \"  piece.t = piece.t + \" ?spd \"; if btn(3) then piece.t = piece.t + 6 end\" (nl)\n"
    "                    \"  if piece.t >= 18 then piece.t = 0\" (nl)\n"
    "                    \"    if piece.y < 13 and board[piece.y + 1][piece.x] == 0 then piece.y = piece.y + 1\" (nl)\n"
    "                    \"    else board[piece.y][piece.x] = piece.col\" (nl)\n"
    "                    \"      local full = true; for x = 0, 9 do if board[piece.y][x] == 0 then full = false end end\" (nl)\n"
    "                    \"      if full then score = score + 100; for yy = piece.y, 1, -1 do board[yy] = board[yy - 1] end; board[0] = {}; for x = 0, 9 do board[0][x] = 0 end else score = score + 10 end\" (nl)\n"
    "                    \"      piece.x, piece.y = 4, 0; if board[0][4] ~= 0 then state = 'gameover' end\" (nl)\n"
    "                    \"    end\" (nl)\n"
    "                    \"  end\")\n"
    "           (str-cat \"  rect(96, 12, 220, 166, 7)\" (nl)\n"
    "                    \"  for y = 0, 13 do for x = 0, 9 do if board[y][x] ~= 0 then rectfill(100 + x * 12, 16 + y * 10, 110 + x * 12, 24 + y * 10, board[y][x]) end end end\" (nl)\n"
    "                    \"  rectfill(100 + piece.x * 12, 16 + piece.y * 10, 110 + piece.x * 12, 24 + piece.y * 10, piece.col)\" (nl)\n"
    "                    \"  print('SCORE:' .. score, 4, 4, 7)\" (nl)\n"
    "                    \"  if state ~= 'play' then print('GAME OVER (A)', 112, 84, 10) end\")))\n"
    "    ((racing-core ?spd)\n"
    "     (list (str-cat \"  state = 'play'; score = 0; lives = 3; ifr = 0; road_y = 0\" (nl)\n"
    "                    \"  car = { x = 152, y = 138, w = 14, h = 18, v = \" ?spd \" }\" (nl)\n"
    "                    \"  obs = { {x=110,y=-20,w=16,h=16}, {x=190,y=-100,w=16,h=16} }\")\n"
    "           (str-cat \"  if state ~= 'play' then if btnp(4) then _init() end; return end\" (nl)\n"
    "                    \"  if ifr > 0 then ifr = ifr - 1 end\" (nl)\n"
    "                    \"  if btn(0) then car.x = math.max(66, car.x - \" ?spd \") end\" (nl)\n"
    "                    \"  if btn(1) then car.x = math.min(238, car.x + \" ?spd \") end\" (nl)\n"
    "                    \"  car.v = btn(2) and \" (+ ?spd 2) \" or (btn(3) and 1 or \" ?spd \")\" (nl)\n"
    "                    \"  road_y = (road_y + car.v) % 24\" (nl)\n"
    "                    \"  for i, o in ipairs(obs) do o.y = o.y + car.v\" (nl)\n"
    "                    \"    if o.y > 180 then o.y = -20; o.x = 72 + ((score + i * 37) * 19) % 156; score = score + 10 end\" (nl)\n"
    "                    \"    if ifr == 0 and car.x < o.x + o.w and car.x + car.w > o.x and car.y < o.y + o.h and car.y + car.h > o.y then\" (nl)\n"
    "                    \"      lives = lives - 1; ifr = 45; if lives <= 0 then state = 'gameover' end\" (nl)\n"
    "                    \"    end\" (nl)\n"
    "                    \"  end\")\n"
    "           (str-cat \"  rectfill(60, 0, 259, 179, 5)\" (nl)\n"
    "                    \"  for y = -24, 180, 24 do rectfill(158, y + road_y, 161, y + road_y + 12, 7) end\" (nl)\n"
    "                    \"  for _, o in ipairs(obs) do rectfill(o.x, o.y, o.x + o.w, o.y + o.h, 8) end\" (nl)\n"
    "                    \"  if ifr % 4 < 2 then rectfill(car.x, car.y, car.x + car.w, car.y + car.h, 11) end\" (nl)\n"
    "                    \"  print('SCORE:' .. score .. ' LIVES:' .. lives, 4, 4, 7)\" (nl)\n"
    "                    \"  if state ~= 'play' then print('CRASH! GAME OVER (A)', 88, 84, 10) end\")))\n"
    "    ((rhythm-core ?spd)\n"
    "     (list (str-cat \"  state = 'play'; score = 0; combo = 0; miss = 0\" (nl)\n"
    "                    \"  notes = { {x=130,y=0,k=0}, {x=160,y=-60,k=4}, {x=190,y=-120,k=1} }; hit_y = 144\")\n"
    "           (str-cat \"  if state ~= 'play' then if btnp(4) then _init() end; return end\" (nl)\n"
    "                    \"  for _, n in ipairs(notes) do n.y = n.y + \" ?spd (nl)\n"
    "                    \"    if btnp(n.k) and math.abs(n.y - hit_y) < 16 then combo = combo + 1; score = score + 50 * combo; n.y = -30\" (nl)\n"
    "                    \"    elseif n.y > 176 then combo = 0; miss = miss + 1; n.y = -20; if miss >= 5 then state = 'gameover' end end\" (nl)\n"
    "                    \"  end\")\n"
    "           (str-cat \"  line(110, hit_y, 210, hit_y, 7)\" (nl)\n"
    "                    \"  for _, n in ipairs(notes) do circfill(n.x, n.y, 6, 10) end\" (nl)\n"
    "                    \"  print('SCORE:' .. score .. ' COMBO:' .. combo .. ' MISS:' .. miss, 4, 4, 7)\" (nl)\n"
    "                    \"  if state ~= 'play' then print('GAME OVER (A)', 112, 84, 10) end\")))\n"
    "    ((fighter-core ?spd)\n"
    "     (list (str-cat \"  state = 'play'; p1 = { x = 80, y = 128, w = 14, h = 24, hp = 100, atk = 0, cd = 0 }\" (nl)\n"
    "                    \"  p2 = { x = 220, y = 128, w = 14, h = 24, hp = 100, dir = -1, atk = 0, cd = 20 }\")\n"
    "           (str-cat \"  if state ~= 'play' then if btnp(4) then _init() end; return end\" (nl)\n"
    "                    \"  if btn(0) then p1.x = math.max(16, p1.x - \" ?spd \") end\" (nl)\n"
    "                    \"  if btn(1) then p1.x = math.min(290, p1.x + \" ?spd \") end\" (nl)\n"
    "                    \"  if p1.cd > 0 then p1.cd = p1.cd - 1 elseif btnp(4) then p1.atk = 8; p1.cd = 18 end\" (nl)\n"
    "                    \"  if p1.atk > 0 then p1.atk = p1.atk - 1; if p1.atk == 7 and math.abs(p1.x - p2.x) < 26 then p2.hp = math.max(0, p2.hp - 15) end end\" (nl)\n"
    "                    \"  if math.abs(p1.x - p2.x) > 20 then p2.x = p2.x + (p2.x > p1.x and -1 or 1)\" (nl)\n"
    "                    \"  elseif p2.cd <= 0 then p2.atk = 8; p2.cd = 35; p1.hp = math.max(0, p1.hp - 10) end\" (nl)\n"
    "                    \"  if p2.cd > 0 then p2.cd = p2.cd - 1 end; if p2.atk > 0 then p2.atk = p2.atk - 1 end\" (nl)\n"
    "                    \"  if p1.hp <= 0 then state = 'lose' elseif p2.hp <= 0 then state = 'win' end\")\n"
    "           (str-cat \"  rectfill(0, 152, 319, 179, 4)\" (nl)\n"
    "                    \"  rectfill(12, 8, 12 + p1.hp, 14, 11); rectfill(200, 8, 200 + p2.hp, 14, 8)\" (nl)\n"
    "                    \"  rectfill(p1.x, p1.y, p1.x + p1.w, p1.y + p1.h, 11)\" (nl)\n"
    "                    \"  if p1.atk > 0 then rectfill(p1.x + p1.w, p1.y + 6, p1.x + p1.w + 10, p1.y + 12, 10) end\" (nl)\n"
    "                    \"  rectfill(p2.x, p2.y, p2.x + p2.w, p2.y + p2.h, 8)\" (nl)\n"
    "                    \"  if p2.atk > 0 then rectfill(p2.x - 10, p2.y + 6, p2.x, p2.y + 12, 9) end\" (nl)\n"
    "                    \"  if state ~= 'play' then print(state == 'win' and 'K.O.! YOU WIN (A)' or 'K.O.! YOU LOSE (A)', 92, 80, 10) end\")))\n"
    "    (_ (list \"\" \"\" \"\"))))\n"
    "(define (expand-goal tgt g)\n"
    "  (match g\n"
    "    ((move-4dir ?name ?spd ?wflag)\n"
    "     (let ((fname (sym-join 'move ?name)))\n"
    "       (if (<= ?wflag 0)\n"
    "           `(fn ,fname ((x i32) (y i32)) i32\n"
    "              (if (call btn 0) ((set x (binop - x ,?spd))))\n"
    "              (if (call btn 1) ((set x (binop + x ,?spd))))\n"
    "              (if (call btn 2) ((set y (binop - y ,?spd))))\n"
    "              (if (call btn 3) ((set y (binop + y ,?spd))))\n"
    "              (return x))\n"
    "           (if (eq? tgt 'bml)\n"
    "               `(fn ,fname ((x i32) (y i32) (grid i32[]) (cols i32)) i32\n"
    "                  (var nx i32 x)\n"
    "                  (var ny i32 y)\n"
    "                  (if (call btn 0) ((set nx (binop - nx ,?spd))))\n"
    "                  (if (call btn 1) ((set nx (binop + nx ,?spd))))\n"
    "                  (if (call btn 2) ((set ny (binop - ny ,?spd))))\n"
    "                  (if (call btn 3) ((set ny (binop + ny ,?spd))))\n"
    "                  (var idx i32 (binop + (binop * (binop // ny 8) cols) (binop + (binop // nx 8) 1)))\n"
    "                  (if (binop ~= grid[idx] ,?wflag)\n"
    "                      ((set x nx) (set y ny)))\n"
    "                  (return x))\n"
    "               `(fn ,fname ((x i32) (y i32)) i32\n"
    "                  (var nx i32 x)\n"
    "                  (var ny i32 y)\n"
    "                  (if (call btn 0) ((set nx (binop - nx ,?spd))))\n"
    "                  (if (call btn 1) ((set nx (binop + nx ,?spd))))\n"
    "                  (if (call btn 2) ((set ny (binop - ny ,?spd))))\n"
    "                  (if (call btn 3) ((set ny (binop + ny ,?spd))))\n"
    "                  (if (unop not (call fget (call mget (binop // nx 8) (binop // ny 8)) ,?wflag))\n"
    "                      ((set x nx) (set y ny)))\n"
    "                  (return x))))))\n"
    "    ((move-8dir ?name ?spd)\n"
    "     `(fn ,(sym-join 'move8 ?name) ((x i32) (y i32)) i32\n"
    "        (var dx i32 0)\n"
    "        (var dy i32 0)\n"
    "        (if (call btn 0) ((set dx -1)))\n"
    "        (if (call btn 1) ((set dx 1)))\n"
    "        (if (call btn 2) ((set dy -1)))\n"
    "        (if (call btn 3) ((set dy 1)))\n"
    "        (set x (binop + x (binop * dx ,?spd)))\n"
    "        (set y (binop + y (binop * dy ,?spd)))\n"
    "        (return x)))\n"
    "    ((tile-wall ?name ?tsz ?wflag)\n"
    "     (if (eq? tgt 'bml)\n"
    "         `(fn ,(sym-join 'blocked ?name) ((px i32) (py i32) (grid i32[]) (cols i32)) bool\n"
    "            (var tx i32 (binop // px ,?tsz))\n"
    "            (var ty i32 (binop // py ,?tsz))\n"
    "            (var idx i32 (binop + (binop * ty cols) (binop + tx 1)))\n"
    "            (return (binop == grid[idx] ,?wflag)))\n"
    "         `(fn ,(sym-join 'blocked ?name) ((px i32) (py i32)) bool\n"
    "            (var tx i32 (binop // px ,?tsz))\n"
    "            (var ty i32 (binop // py ,?tsz))\n"
    "            (return (call fget (call mget tx ty) ,?wflag)))))\n"
    "    ((aabb-overlap ?name ?w ?h)\n"
    "     `(fn ,(sym-join 'hit ?name) ((ax i32) (ay i32) (bx i32) (by i32)) bool\n"
    "        (return (binop and (binop < (call abs (binop - ax bx)) ,?w)\n"
    "                           (binop < (call abs (binop - ay by)) ,?h)))))\n"
    "    ((cooldown ?name ?frames)\n"
    "     `(fn ,(sym-join 'tick ?name) ((cd i32) (trig bool)) i32\n"
    "        (if (binop > cd 0) ((return (binop - cd 1))))\n"
    "        (if trig ((return ,?frames)))\n"
    "        (return 0)))\n"
    "    ((platform-jump ?name ?grav ?jv)\n"
    "     `(fn ,(sym-join 'jump ?name) ((vy i32) (ground bool)) i32\n"
    "        (if (binop and ground (call btnp 4))\n"
    "            ((return ,(- 0 ?jv))))\n"
    "        (return (binop + vy ,?grav))))\n"
    "    ((clamp-fn ?name ?lo ?hi)\n"
    "     `(fn ,(sym-join 'clamp ?name) ((v i32)) i32\n"
    "        (return (call min (call max v ,?lo) ,?hi))))\n"
    "    ((grid-index ?name ?cols)\n"
    "     `(fn ,(sym-join 'idx ?name) ((gx i32) (gy i32)) i32\n"
    "        (return (binop + (binop * gy ,?cols) (binop + gx 1)))))\n"
    "    ((entity-loop ?name ?count)\n"
    "     `(fn ,(sym-join 'step ?name) ((xs i32[]) (vxs i32[])) i32\n"
    "        (var active i32 0)\n"
    "        (for i 1 ,?count\n"
    "          ((if (binop ~= xs[i] 0)\n"
    "               ((set active (binop + active 1))))))\n"
    "        (return active)))\n"
    "    ((patrol-chase ?name ?spd ?range)\n"
    "     `(fn ,(sym-join 'patrol ?name) ((ex i32) (px i32) (dir i32)) i32\n"
    "        (var dx i32 (binop * dir ,?spd))\n"
    "        (if (binop < (call abs (binop - px ex)) ,?range)\n"
    "            ((if (binop < ex px)\n"
    "                 ((set dx ,?spd))\n"
    "                 ((set dx ,(- 0 ?spd))))))\n"
    "        (return (binop + ex dx))))\n"
    "    ((lerp-fn ?name)\n"
    "     `(fn ,(sym-join 'lerp ?name) ((a i32) (b i32) (t i32) (scale i32)) i32\n"
    "        (return (binop + a (binop // (binop * (binop - b a) t) scale)))))\n"
    "    ((damage-hp ?name)\n"
    "     `(fn ,(sym-join 'dmg ?name) ((hp i32) (dmg i32) (ifr i32)) i32\n"
    "        (if (binop > ifr 0) ((return hp)))\n"
    "        (return (call max 0 (binop - hp dmg)))))\n"
    "    ((wired-actor ?name ?spd ?lo ?hi)\n"
    "     (let ((mfn (sym-join 'move ?name))\n"
    "           (cfn (sym-join 'clamp ?name))\n"
    "           (sfn (sym-join 'step ?name)))\n"
    "       `(seq\n"
    "          (fn ,mfn ((x i32) (y i32)) i32\n"
    "            (if (call btn 0) ((set x (binop - x ,?spd))))\n"
    "            (if (call btn 1) ((set x (binop + x ,?spd))))\n"
    "            (if (call btn 2) ((set y (binop - y ,?spd))))\n"
    "            (if (call btn 3) ((set y (binop + y ,?spd))))\n"
    "            (return x))\n"
    "          (fn ,cfn ((v i32)) i32\n"
    "            (return (call min (call max v ,?lo) ,?hi)))\n"
    "          (fn ,sfn ((x i32) (y i32)) i32\n"
    "            (var nx i32 (call ,mfn x y))\n"
    "            (return (call ,cfn nx))))))\n"
    "    ((platformer-cart ?name ?spd ?grav ?jv)\n"
    "     (str-cat\n"
    "       \"-- Geometric Platformer Cartridge (\" ?name \")\" (nl)\n"
    "       \"local SPD, GRAV, JV = \" ?spd \", \" ?grav \", \" ?jv (nl)\n"
    "       \"local TILE, COLS = 16, 60\" (nl)\n"
    "       \"cam_x, score, lives, state, flag_x = 0, 0, 3, 'play', 880\" (nl)\n"
    "       \"local function tile_at(tx, ty)\" (nl)\n"
    "       \"  if tx < 0 or tx >= COLS or ty < 0 then return 0 end\" (nl)\n"
    "       \"  if ty >= 10 then\" (nl)\n"
    "       \"    if (tx >= 14 and tx <= 15) or (tx >= 30 and tx <= 31) then return 0 end\" (nl)\n"
    "       \"    return 1\" (nl)\n"
    "       \"  end\" (nl)\n"
    "       \"  if ty == 9 and (tx == 10 or tx == 22 or tx == 38) then return 3 end\" (nl)\n"
    "       \"  if ty == 8 and (tx == 22 or tx == 38) then return 3 end\" (nl)\n"
    "       \"  if ty == 7 and ((tx >= 6 and tx <= 9) or (tx >= 18 and tx <= 21) or (tx >= 33 and tx <= 36)) then return 2 end\" (nl)\n"
    "       \"  return 0\" (nl)\n"
    "       \"end\" (nl)\n"
    "       \"local function solid_at(px, py) return tile_at(math.floor(px / TILE), math.floor(py / TILE)) > 0 end\" (nl)\n"
    "       \"function _init()\" (nl)\n"
    "       \"  cam_x, score, lives, state = 0, 0, 3, 'play'\" (nl)\n"
    "       \"  player = { x = 32, y = 146, w = 12, h = 14, vx = 0, vy = 0, on_ground = false, ifr = 0 }\" (nl)\n"
    "       \"  enemies = { {x=180,y=148,w=12,h=12,dir=-1,alive=true}, {x=390,y=148,w=12,h=12,dir=-1,alive=true}, {x=620,y=148,w=12,h=12,dir=1,alive=true} }\" (nl)\n"
    "       \"  coins = { {x=116,y=96,got=false}, {x=132,y=96,got=false}, {x=308,y=96,got=false}, {x=548,y=96,got=false}, {x=720,y=140,got=false} }\" (nl)\n"
    "       \"end\" (nl)\n"
    "       \"local function step_player()\" (nl)\n"
    "       \"  if player.ifr > 0 then player.ifr = player.ifr - 1 end\" (nl)\n"
    "       \"  local vx = 0\" (nl)\n"
    "       \"  if btn(0) then vx = -SPD end\" (nl)\n"
    "       \"  if btn(1) then vx = SPD end\" (nl)\n"
    "       \"  player.vx = vx\" (nl)\n"
    "       \"  local nx = player.x + vx\" (nl)\n"
    "       \"  if nx >= 0 and not solid_at(nx, player.y + 2) and not solid_at(nx + player.w - 1, player.y + 2) and not solid_at(nx, player.y + player.h - 2) and not solid_at(nx + player.w - 1, player.y + player.h - 2) then\" (nl)\n"
    "       \"    player.x = nx\" (nl)\n"
    "       \"  else player.vx = 0 end\" (nl)\n"
    "       \"  if player.on_ground and btnp(4) then player.vy = -JV; player.on_ground = false end\" (nl)\n"
    "       \"  player.vy = math.min(player.vy + GRAV, 8)\n\"\n"
    "       \"  local ny = player.y + player.vy\" (nl)\n"
    "       \"  player.on_ground = false\" (nl)\n"
    "       \"  if player.vy >= 0 then\" (nl)\n"
    "       \"    if solid_at(player.x + 2, ny + player.h) or solid_at(player.x + player.w - 2, ny + player.h) then\" (nl)\n"
    "       \"      player.y = math.floor((ny + player.h) / TILE) * TILE - player.h; player.vy = 0; player.on_ground = true\" (nl)\n"
    "       \"    else player.y = ny end\" (nl)\n"
    "       \"  else\" (nl)\n"
    "       \"    if solid_at(player.x + 2, ny) or solid_at(player.x + player.w - 2, ny) then player.vy = 0 else player.y = ny end\" (nl)\n"
    "       \"  end\" (nl)\n"
    "       \"  if player.y > 192 then\" (nl)\n"
    "       \"    lives = lives - 1\" (nl)\n"
    "       \"    if lives <= 0 then state = 'gameover' else player.x, player.y, player.vy = 32, 146, 0 end\" (nl)\n"
    "       \"  end\" (nl)\n"
    "       \"  cam_x = math.max(0, math.min(player.x - 120, COLS * TILE - 320))\" (nl)\n"
    "       \"end\" (nl)\n"
    "       \"local function step_world()\" (nl)\n"
    "       \"  for _, e in ipairs(enemies) do if e.alive then\" (nl)\n"
    "       \"    local ax = e.x + e.dir\" (nl)\n"
    "       \"    if ax < 16 or solid_at(ax + (e.dir > 0 and e.w or 0), e.y + 6) or not solid_at(ax + 6, e.y + e.h + 4) then e.dir = -e.dir else e.x = ax end\" (nl)\n"
    "       \"    if math.abs((player.x + 6) - (e.x + 6)) < 11 and math.abs((player.y + 7) - (e.y + 6)) < 12 then\" (nl)\n"
    "       \"      if player.vy > 0 and (player.y + player.h) <= (e.y + 8) then e.alive = false; player.vy = -4; score = score + 100\" (nl)\n"
    "       \"      elseif player.ifr == 0 then lives = lives - 1; player.ifr = 45; if lives <= 0 then state = 'gameover' end end\" (nl)\n"
    "       \"    end\" (nl)\n"
    "       \"  end end\" (nl)\n"
    "       \"  for _, c in ipairs(coins) do if not c.got then\" (nl)\n"
    "       \"    if math.abs((player.x + 6) - c.x) < 12 and math.abs((player.y + 7) - c.y) < 12 then c.got = true; score = score + 50 end\" (nl)\n"
    "       \"  end end\" (nl)\n"
    "       \"  if player.x + player.w >= flag_x then state = 'win'; score = score + 500 end\" (nl)\n"
    "       \"end\" (nl)\n"
    "       \"function _update()\" (nl)\n"
    "       \"  if state ~= 'play' then if btnp(4) then _init() end; return end\" (nl)\n"
    "       \"  step_player(); step_world()\" (nl)\n"
    "       \"end\" (nl)\n"
    "       \"function _draw()\" (nl)\n"
    "       \"  cls(12)\" (nl)\n"
    "       \"  local t0 = math.floor(cam_x / TILE)\" (nl)\n"
    "       \"  for tx = t0, t0 + 21 do for ty = 0, 11 do local t = tile_at(tx, ty); if t > 0 then\" (nl)\n"
    "       \"    local sx, sy = tx * TILE - cam_x, ty * TILE\" (nl)\n"
    "       \"    rectfill(sx, sy, sx + TILE - 1, sy + TILE - 1, t == 1 and 4 or (t == 2 and 9 or 11))\" (nl)\n"
    "       \"  end end end\" (nl)\n"
    "       \"  rectfill(flag_x - cam_x, 48, flag_x - cam_x + 3, 160, 7)\" (nl)\n"
    "       \"  rectfill(flag_x - cam_x + 3, 50, flag_x - cam_x + 16, 62, 8)\" (nl)\n"
    "       \"  for _, c in ipairs(coins) do if not c.got then circfill(c.x - cam_x, c.y, 4, 10) end end\" (nl)\n"
    "       \"  for _, e in ipairs(enemies) do if e.alive then rectfill(e.x - cam_x, e.y, e.x - cam_x + e.w, e.y + e.h, 8) end end\" (nl)\n"
    "       \"  rectfill(player.x - cam_x, player.y, player.x - cam_x + player.w, player.y + player.h, 11)\" (nl)\n"
    "       \"  print('SCORE:' .. score .. ' LIVES:' .. lives, 6, 6, 7)\" (nl)\n"
    "       \"  if state ~= 'play' then print(state == 'win' and 'YOU WIN!' or 'GAME OVER', 120, 80, 10) end\" (nl)\n"
    "       \"end\"))\n"
    "    ((compose . ?goals)\n"
    "     (cons 'seq (map (lambda (x) (expand-goal tgt x)) ?goals)))\n"
    "    ((wire-cart . ?parts)\n"
    "     (let ((ps (map wire-part ?parts)))\n"
    "       (let ((inits (join-with (nl) (filter (lambda (s) (not (equal? s \"\"))) (map car ps))))\n"
    "             (upds  (join-with (nl) (filter (lambda (s) (not (equal? s \"\"))) (map (lambda (x) (car (cdr x))) ps))))\n"
    "             (draws (join-with (nl) (filter (lambda (s) (not (equal? s \"\"))) (map (lambda (x) (car (cdr (cdr x)))) ps)))))\n"
    "         (str-cat \"function _init()\" (nl) inits (nl) \"end\" (nl)\n"
    "                  \"function _update()\" (nl) upds (nl) \"end\" (nl)\n"
    "                  \"function _draw()\" (nl) \"  cls(0)\" (nl) draws (nl) \"end\"))))\n"
    "    (_ g)))\n"
    "(define (synth tgt goal)\n"
    "  (emit-ast tgt (expand-goal tgt goal)))\n";

/* Example-driven arithmetic/linear+quadratic expression synthesizer (symbolic search) */
static const char *LISP_SOLVER =
    "(define (fits-all? a b exs)\n"
    "  (cond ((null? exs) #t)\n"
    "        ((= (+ (* a (car (car exs))) b) (car (cdr (car exs))))\n"
    "         (fits-all? a b (cdr exs)))\n"
    "        (else #f)))\n"
    "(define (search-lin exs a)\n"
    "  (if (> a 32) #f\n"
    "      (let ((b (- (car (cdr (car exs))) (* a (car (car exs))))))\n"
    "        (if (fits-all? a b (cdr exs))\n"
    "            (list a b)\n"
    "            (search-lin exs (+ a 1))))))\n"
    "(define (fits-quad? c a b exs)\n"
    "  (cond ((null? exs) #t)\n"
    "        (else (let ((x (car (car exs))) (y (car (cdr (car exs)))))\n"
    "                (if (= (+ (* c (* x x)) (* a x) b) y)\n"
    "                    (fits-quad? c a b (cdr exs))\n"
    "                    #f)))))\n"
    "(define (search-quad exs c)\n"
    "  (if (> c 8) #f\n"
    "      (let ((x0 (car (car exs))) (y0 (car (cdr (car exs))))\n"
    "            (x1 (car (car (cdr exs)))) (y1 (car (cdr (car (cdr exs))))))\n"
    "        (let ((dx (- x1 x0)))\n"
    "          (if (= dx 0) #f\n"
    "              (let ((num (- (- y1 (* c (* x1 x1))) (- y0 (* c (* x0 x0))))))\n"
    "                (if (= (mod num dx) 0)\n"
    "                    (let ((a (/ num dx)))\n"
    "                      (let ((b (- y0 (+ (* c (* x0 x0)) (* a x0)))))\n"
    "                        (if (fits-quad? c a b (cdr (cdr exs)))\n"
    "                            (list c a b)\n"
    "                            (search-quad exs (+ c 1)))))\n"
    "                    (search-quad exs (+ c 1)))))))))\n"
    "(define (solve-expr tgt exs var)\n"
    "  (let ((ab (search-lin exs -16)))\n"
    "    (if ab\n"
    "        (let ((a (car ab)) (b (car (cdr ab))))\n"
    "          (let ((term (if (= a 1) var (if (= a 0) 0 `(binop * ,var ,a)))))\n"
    "            (let ((body (cond ((= b 0) term)\n"
    "                              ((= a 0) b)\n"
    "                              (else `(binop + ,term ,b)))))\n"
    "              (emit-ast tgt `(fn calc ((,var i32)) i32 (return ,body))))))\n"
    "        (let ((cab (search-quad exs -8)))\n"
    "          (if (not cab) \"-- no rule found\"\n"
    "              (let ((c (car cab)) (a (car (cdr cab))) (b (car (cdr (cdr cab)))))\n"
    "                (let ((qterm (if (= c 1) `(binop * ,var ,var)\n"
    "                                 `(binop * (binop * ,var ,var) ,c))))\n"
    "                  (let ((with-a (if (= a 0) qterm\n"
    "                                    `(binop + ,qterm ,(if (= a 1) var `(binop * ,var ,a))))))\n"
    "                    (let ((body (if (= b 0) with-a `(binop + ,with-a ,b))))\n"
    "                      (emit-ast tgt `(fn calc ((,var i32)) i32 (return ,body))))))))))))\n";

static int ensure_vm(char *err_out, size_t err_cap)
{
    if (synth_ready) return 0;
    synth_vm = bm_lisp_init(synth_arena, sizeof(synth_arena));
    if (!synth_vm) {
        if (err_out && err_cap) snprintf(err_out, err_cap, "synth: arena init failed");
        return -1;
    }
    if (bm_lisp_eval(synth_vm, LISP_EMITTER, NULL, 0, err_out, err_cap) != 0 ||
        bm_lisp_eval(synth_vm, LISP_PATTERNS, NULL, 0, err_out, err_cap) != 0 ||
        bm_lisp_eval(synth_vm, LISP_SOLVER, NULL, 0, err_out, err_cap) != 0) {
        return -1;
    }
    bm_lisp_freeze_base(synth_vm);
    synth_ready = 1;
    return 0;
}

/* ------------------------------------------------ Hybrid Slot Extractor */

static int has_word(const char *hay, const char *needle)
{
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
        if (tolower((unsigned char)*p) == tolower((unsigned char)needle[0])) {
            size_t i = 0;
            while (i < nlen && p[i] &&
                   tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
                i++;
            if (i == nlen) {
                int left_ok = (p == hay || !isalnum((unsigned char)p[-1]));
                int right_ok = (!isalnum((unsigned char)p[nlen]));
                if (left_ok && right_ok) return 1;
            }
        }
    }
    return 0;
}

static int extract_int_near(const char *q, const char *kw, int def_val)
{
    size_t klen = strlen(kw);
    for (const char *p = q; *p; p++) {
        size_t i = 0;
        while (i < klen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)kw[i]))
            i++;
        if (i == klen) {
            const char *s = p + klen;
            while (isalpha((unsigned char)*s)) s++;
            while (*s && !isdigit((unsigned char)*s) && *s != '-') {
                if (isalpha((unsigned char)*s)) break;
                s++;
            }
            if (isdigit((unsigned char)*s) || (*s == '-' && isdigit((unsigned char)s[1]))) {
                int neg = (*s == '-');
                if (neg) s++;
                int v = 0;
                while (isdigit((unsigned char)*s)) { v = v * 10 + (*s - '0'); s++; }
                return neg ? -v : v;
            }
        }
    }
    return def_val;
}

static void extract_name(const char *q, const char *fallback, char *out, size_t cap)
{
    static const char *const prefixes[] = {
        "entita ", "entity ", "funzione ", "nome ", "name ", "fn ", NULL
    };
    for (int i = 0; prefixes[i]; i++) {
        size_t plen = strlen(prefixes[i]);
        for (const char *p = q; *p; p++) {
            size_t j = 0;
            while (j < plen && p[j] &&
                   tolower((unsigned char)p[j]) == tolower((unsigned char)prefixes[i][j]))
                j++;
            if (j == plen && (p == q || !isalnum((unsigned char)p[-1]))) {
                const char *s = p + plen;
                while (*s == ' ') s++;
                if (isalpha((unsigned char)*s)) {
                    size_t k = 0;
                    while (k + 1 < cap && (isalnum((unsigned char)s[k]) || s[k] == '_')) {
                        out[k] = (char)tolower((unsigned char)s[k]);
                        k++;
                    }
                    out[k] = '\0';
                    if (k > 0) return;
                }
            }
        }
    }
    static const char *const roles[] = {
        "player", "hero", "enemy", "nemico", "bullet", "proiettile",
        "boss", "ship", "nave", "slime", "paddle", "ball", "palla",
        "plat", "piattaforma", NULL
    };
    for (int i = 0; roles[i]; i++) {
        if (has_word(q, roles[i])) {
            snprintf(out, cap, "%s", roles[i]);
            return;
        }
    }
    snprintf(out, cap, "%s", (fallback && fallback[0]) ? fallback : "player");
}

static void extract_wh_pair(const char *q, int *w_out, int *h_out)
{
    for (const char *p = q; *p; p++) {
        if (!isdigit((unsigned char)*p)) continue;
        if (p > q && isalnum((unsigned char)p[-1])) continue;
        const char *s = p;
        int a = 0;
        while (isdigit((unsigned char)*s)) { a = a * 10 + (*s - '0'); s++; }
        while (*s == ' ') s++;
        int sep = 0;
        if ((*s == 'x' || *s == 'X') && (isdigit((unsigned char)s[1]) || s[1] == ' ')) {
            sep = 1;
            s++;
        } else if (strncmp(s, "per ", 4) == 0) {
            sep = 1;
            s += 4;
        }
        if (sep) {
            while (*s == ' ') s++;
            if (isdigit((unsigned char)*s)) {
                int b = 0;
                while (isdigit((unsigned char)*s)) { b = b * 10 + (*s - '0'); s++; }
                if (a > 0 && b > 0) { *w_out = a; *h_out = b; return; }
            }
        }
    }
}

static const char *detect_target(const char *q, const char *target)
{
    if (target && (!strcmp(target, "bml") || !strcmp(target, ".b"))) return "bml";
    if (target && !strcmp(target, "lua")) return "lua";
    if (strstr(q, ".b") || has_word(q, "bml") || has_word(q, "typed") || has_word(q, "native"))
        return "bml";
    return "lua";
}

static int build_clause_goal(const char *q, const char *entry_id, const char *default_name,
                             char *goal, size_t cap, char *used_name, size_t name_cap)
{
    char name[32];
    extract_name(q, default_name, name, sizeof(name));
    if (used_name && name_cap) snprintf(used_name, name_cap, "%s", name);

    int speed = extract_int_near(q, "velocit", -1);
    if (speed < 0) speed = extract_int_near(q, "speed", 2);

    int wflag = extract_int_near(q, "flag", 0);
    int tsz = extract_int_near(q, "tile", 8);
    int w = extract_int_near(q, "larg", -1);
    if (w < 0) w = extract_int_near(q, "w", -1);
    int h = extract_int_near(q, "alt", -1);
    if (h < 0) h = extract_int_near(q, "h", -1);
    if (w < 0 || h < 0) {
        int pw = 16, ph = 16;
        extract_wh_pair(q, &pw, &ph);
        if (w < 0) w = pw;
        if (h < 0) h = ph;
    }
    int cd = extract_int_near(q, "cooldown", -1);
    if (cd < 0) cd = extract_int_near(q, "timer", -1);
    if (cd < 0) cd = extract_int_near(q, "delay", -1);
    if (cd < 0) cd = extract_int_near(q, "frame", 30);
    int grav = extract_int_near(q, "grav", 1);
    int jump_v = extract_int_near(q, "salto", -1);
    if (jump_v < 0) jump_v = extract_int_near(q, "jump", 6);
    int cols = extract_int_near(q, "col", 16);
    int count = extract_int_near(q, "count", -1);
    if (count < 0) count = extract_int_near(q, "entit", 16);
    int range = extract_int_near(q, "range", -1);
    if (range < 0) range = extract_int_near(q, "raggio", 48);
    int col_v = extract_int_near(q, "colore", -1);
    if (col_v < 0) col_v = extract_int_near(q, "color", 11);
    int acc = extract_int_near(q, "accel", -1);
    int fric = extract_int_near(q, "attrit", -1);
    if (fric < 0) fric = extract_int_near(q, "fric", 1);
    int maxv = extract_int_near(q, "max", -1);
    if (maxv < 0) maxv = extract_int_near(q, "fino a", 3);
    int minx = extract_int_near(q, "min", 40);

    int is_rect = has_word(q, "rettangolo") || has_word(q, "rect") || has_word(q, "quadrato");
    int is_accel = (acc >= 0) || has_word(q, "accelerazione") || has_word(q, "accel") ||
                   has_word(q, "accelera") || has_word(q, "attrito") || has_word(q, "friction") ||
                   has_word(q, "frena");
    int is_bounce = has_word(q, "rimbalzante") || has_word(q, "bouncing");
    int is_collide_bounce = has_word(q, "rimbalza");
    int is_moving_plat = has_word(q, "piattaforma") && (has_word(q, "mobile") || has_word(q, "velocita") || has_word(q, "min"));
    int is_land_on = has_word(q, "atterra");
    int is_stomp = has_word(q, "schiaccia") || has_word(q, "stomp");
    int is_patrol = has_word(q, "patrol") || has_word(q, "pattuglia") || has_word(q, "chase") ||
                    has_word(q, "insegui");
    int is_bounds = has_word(q, "min") || has_word(q, "max") || has_word(q, "bordi");

    if (is_bounce) {
        snprintf(goal, cap, "(bounce-actor %s %d %d %d %d)", name, col_v, w, h, speed);
        return 0;
    }
    if (is_collide_bounce) {
        snprintf(goal, cap, "(collide-bounce ball %s)", name);
        return 0;
    }
    if (is_moving_plat) {
        int c_pl = extract_int_near(q, "colore", -1);
        if (c_pl < 0) c_pl = extract_int_near(q, "color", 9);
        snprintf(goal, cap, "(moving-platform %s %d %d %d %d %d %d)", name, speed, minx, maxv, w, h, c_pl);
        return 0;
    }
    if (is_land_on) {
        snprintf(goal, cap, "(land-on %s %s)", default_name, name);
        return 0;
    }
    if (is_rect) {
        snprintf(goal, cap, "(rect-actor %s %d %d %d)", name, col_v, w, h);
        return 0;
    }
    if (is_accel) {
        snprintf(goal, cap, "(accel-x %s %d %d %d)", name, acc >= 0 ? acc : 1, fric, maxv);
        return 0;
    }
    if (is_patrol && is_bounds) {
        int c_en = extract_int_near(q, "colore", -1);
        if (c_en < 0) c_en = extract_int_near(q, "color", 8);
        snprintf(goal, cap, "(patrol-bounds %s %d %d %d %d)", name, speed, minx, maxv, c_en);
        return 0;
    }
    if (is_stomp) {
        snprintf(goal, cap, "(stomp-or-hit %s enemy)", default_name);
        return 0;
    }

    int is_move = has_word(q, "muovi") || has_word(q, "muovere") || has_word(q, "move") ||
                  has_word(q, "frecce") || has_word(q, "arrows") ||
                  (entry_id && strstr(entry_id, "move_arrows"));
    int is_move8 = has_word(q, "8dir") || (has_word(q, "8") && is_move) ||
                   (entry_id && strstr(entry_id, "move_8dir"));
    int is_wall = has_word(q, "muro") || has_word(q, "muri") || has_word(q, "wall") ||
                  has_word(q, "walls") || has_word(q, "mget") || has_word(q, "fget") ||
                  has_word(q, "solid");
    int is_aabb = has_word(q, "aabb") || has_word(q, "hitbox") || has_word(q, "collision") ||
                  has_word(q, "collisione") || has_word(q, "overlap");
    int is_jump = has_word(q, "salto") || has_word(q, "jump") || has_word(q, "gravit") ||
                  has_word(q, "gravity") || has_word(q, "platform");
    int is_cd = has_word(q, "cooldown") || has_word(q, "timer") || has_word(q, "sparo") ||
                has_word(q, "shoot") || has_word(q, "delay");
    int is_clamp = has_word(q, "clamp") || has_word(q, "limita") || has_word(q, "bordo");
    int is_grid = has_word(q, "indice") || has_word(q, "index") || has_word(q, "griglia") ||
                  has_word(q, "grid");
    int is_loop = has_word(q, "array") || has_word(q, "pool") || has_word(q, "particelle");
    int is_lerp = has_word(q, "lerp") || has_word(q, "interpola") || has_word(q, "smooth");
    int is_dmg = has_word(q, "danno") || has_word(q, "damage") || has_word(q, "hp") ||
                 has_word(q, "vita");

    if (is_move && is_wall) {
        int wf = wflag > 0 ? wflag : 1;
        snprintf(goal, cap, "(move-4dir %s %d %d)", name, speed, wf);
        return 0;
    }
    if (is_move8) {
        snprintf(goal, cap, "(move-8dir %s %d)", name, speed);
        return 0;
    }
    if (is_move) {
        snprintf(goal, cap, "(move-4dir %s %d %d)", name, speed, wflag);
        return 0;
    }
    if (is_wall) {
        int wf = wflag > 0 ? wflag : 1;
        snprintf(goal, cap, "(tile-wall %s %d %d)", name, tsz, wf);
        return 0;
    }
    if (is_aabb) {
        snprintf(goal, cap, "(aabb-overlap %s %d %d)", name, w, h);
        return 0;
    }
    if (is_jump) {
        snprintf(goal, cap, "(platform-jump %s %d %d)", name, grav, jump_v);
        return 0;
    }
    if (is_cd) {
        snprintf(goal, cap, "(cooldown %s %d)", name, cd);
        return 0;
    }
    if (is_clamp) {
        snprintf(goal, cap, "(clamp-fn %s 0 255)", name);
        return 0;
    }
    if (is_grid) {
        snprintf(goal, cap, "(grid-index %s %d)", name, cols);
        return 0;
    }
    if (is_loop) {
        snprintf(goal, cap, "(entity-loop %s %d)", name, count);
        return 0;
    }
    if (is_patrol) {
        snprintf(goal, cap, "(patrol-chase %s %d %d)", name, speed, range);
        return 0;
    }
    if (is_lerp) {
        snprintf(goal, cap, "(lerp-fn %s)", name);
        return 0;
    }
    if (is_dmg) {
        snprintf(goal, cap, "(damage-hp %s)", name);
        return 0;
    }
    return -1;
}

static size_t match_conj_sep(const char *p)
{
    if (*p == ',' || *p == ';' || *p == '\n' || *p == '+') return 1;
    static const char *const seps[] = { " e ", " poi ", " and ", " then ", NULL };
    for (int i = 0; seps[i]; i++) {
        size_t len = strlen(seps[i]);
        size_t j = 0;
        while (j < len && p[j] &&
               tolower((unsigned char)p[j]) == tolower((unsigned char)seps[i][j]))
            j++;
        if (j == len) return len;
    }
    return 0;
}

static int is_wire_part_goal(const char *g)
{
    return strncmp(g, "(rect-actor ", 12) == 0 ||
           strncmp(g, "(accel-x ", 9) == 0 ||
           strncmp(g, "(gravity-floor ", 15) == 0 ||
           strncmp(g, "(patrol-bounds ", 15) == 0 ||
           strncmp(g, "(stomp-or-hit ", 14) == 0 ||
           strncmp(g, "(bounce-actor ", 14) == 0 ||
           strncmp(g, "(collide-bounce ", 16) == 0 ||
           strncmp(g, "(moving-platform ", 17) == 0 ||
           strncmp(g, "(land-on ", 9) == 0;
}

static int build_goal_from_nl(const char *q, const char *entry_id, char *goal, size_t cap)
{
    if (entry_id && strncmp(entry_id, "genre.", 6) == 0) {
        int speed = extract_int_near(q, "velocit", -1);
        if (speed < 0) speed = extract_int_near(q, "speed", 2);
        int grav = extract_int_near(q, "grav", 1);
        int jump_v = extract_int_near(q, "salto", -1);
        if (jump_v < 0) jump_v = extract_int_near(q, "jump", 6);
        int btn_v = extract_int_near(q, "tasto", -1);
        if (btn_v < 0) btn_v = extract_int_near(q, "btn", 4);
        int cd = extract_int_near(q, "cooldown", 10);

        if (strcmp(entry_id, "genre.platformer") == 0) {
            snprintf(goal, cap,
                     "(wire-cart (rect-actor hero 11 12 14) (accel-x hero 1 1 %d) "
                     "(gravity-floor hero %d %d %d 144) (patrol-bounds enemy 1 80 240 8) "
                     "(stomp-or-hit hero enemy))",
                     speed, grav, jump_v, btn_v);
            return 0;
        }
        if (strcmp(entry_id, "genre.shmup") == 0) {
            snprintf(goal, cap, "(wire-cart (shmup-core %d %d))", speed > 0 ? speed : 3, cd);
            return 0;
        }
        if (strcmp(entry_id, "genre.breakout") == 0) {
            snprintf(goal, cap, "(wire-cart (breakout-core %d))", speed > 0 ? speed : 4);
            return 0;
        }
        if (strcmp(entry_id, "genre.topdown") == 0) {
            snprintf(goal, cap, "(wire-cart (topdown-core %d))", speed > 0 ? speed : 2);
            return 0;
        }
        if (strcmp(entry_id, "genre.runner") == 0) {
            snprintf(goal, cap, "(wire-cart (runner-core %d %d %d))", speed > 0 ? speed : 3, grav, jump_v);
            return 0;
        }
        if (strcmp(entry_id, "genre.match_puzzle") == 0) {
            snprintf(goal, cap, "(wire-cart (match-puzzle-core %d))", speed > 0 ? speed : 1);
            return 0;
        }
        if (strcmp(entry_id, "genre.logic_puzzle") == 0 ||
            strcmp(entry_id, "genre.turn_rpg") == 0 ||
            strcmp(entry_id, "genre.tactical_rpg") == 0 ||
            strcmp(entry_id, "genre.strategy") == 0) {
            snprintf(goal, cap, "(wire-cart (grid-cursor-core 10 7))");
            return 0;
        }
        if (strcmp(entry_id, "genre.racing") == 0 ||
            strcmp(entry_id, "genre.simulation") == 0) {
            snprintf(goal, cap, "(wire-cart (racing-core %d))", speed > 0 ? speed : 3);
            return 0;
        }
        if (strcmp(entry_id, "genre.rhythm_music") == 0) {
            snprintf(goal, cap, "(wire-cart (rhythm-core %d))", speed > 0 ? speed : 3);
            return 0;
        }
        if (strcmp(entry_id, "genre.pinball") == 0) {
            snprintf(goal, cap, "(wire-cart (breakout-core %d))", speed > 0 ? speed : 4);
            return 0;
        }
        if (strcmp(entry_id, "genre.beatemup") == 0 ||
            strcmp(entry_id, "genre.fighting") == 0) {
            snprintf(goal, cap, "(wire-cart (fighter-core %d))", speed > 0 ? speed : 3);
            return 0;
        }
        if (strcmp(entry_id, "genre.action_rpg") == 0 ||
            strcmp(entry_id, "genre.dungeon_crawler") == 0 ||
            strcmp(entry_id, "genre.sports") == 0 ||
            strcmp(entry_id, "genre.maze_chase") == 0 ||
            strcmp(entry_id, "genre.stealth") == 0 ||
            strcmp(entry_id, "genre.adventure") == 0 ||
            strcmp(entry_id, "genre.multiplayer_party") == 0 ||
            strcmp(entry_id, "genre.survival") == 0) {
            snprintf(goal, cap, "(wire-cart (topdown-core %d))", speed > 0 ? speed : 2);
            return 0;
        }
    }

    int is_game = has_word(q, "gioco") || has_word(q, "game") ||
                  has_word(q, "mario") || has_word(q, "cartuccia") || has_word(q, "cartridge");
    int is_plat = has_word(q, "platform") || has_word(q, "platformer") || has_word(q, "mario");
    if (is_game && is_plat) {
        char name[32];
        extract_name(q, "mario", name, sizeof(name));
        int speed = extract_int_near(q, "velocit", -1);
        if (speed < 0) speed = extract_int_near(q, "speed", 2);
        int grav = extract_int_near(q, "grav", 1);
        int jump_v = extract_int_near(q, "salto", -1);
        if (jump_v < 0) jump_v = extract_int_near(q, "jump", 6);
        snprintf(goal, cap, "(platformer-cart %s %d %d %d)", name, speed, grav, jump_v);
        return 0;
    }

    int wire_ctx = has_word(q, "rettangolo") || has_word(q, "rect") ||
                   has_word(q, "quadrato") || has_word(q, "pavimento") ||
                   has_word(q, "floor") || has_word(q, "fermandosi") ||
                   has_word(q, "accelerazione") || has_word(q, "accel") ||
                   has_word(q, "accelera") || has_word(q, "schiaccia") ||
                   has_word(q, "stomp") || has_word(q, "rimbalzante") ||
                   has_word(q, "piattaforma");
    int has_grav_clause = has_word(q, "gravita") || has_word(q, "gravity") ||
                          has_word(q, "pavimento") || has_word(q, "floor") ||
                          has_word(q, "fermandosi");
    int added_grav_wire = 0;
    int added_accel_wire = 0;

    static char clauses[16][192];
    int n_goals = 0;
    char default_name[32] = "player";
    int have_default_name = 0;

    const char *cur = q;
    while (*cur && n_goals < 16) {
        while (*cur && isspace((unsigned char)*cur)) cur++;
        if (!*cur) break;
        const char *end = cur;
        size_t sep_len = 0;
        while (*end) {
            sep_len = match_conj_sep(end);
            if (sep_len > 0) break;
            end++;
        }
        size_t clen = (size_t)(end - cur);
        if (clen > 0) {
            char cbuf[256];
            if (clen >= sizeof(cbuf)) clen = sizeof(cbuf) - 1;
            memcpy(cbuf, cur, clen);
            cbuf[clen] = '\0';

            int c_has_rect = has_word(cbuf, "rettangolo") || has_word(cbuf, "rect") || has_word(cbuf, "quadrato");
            int c_has_accel = has_word(cbuf, "accelerazione") || has_word(cbuf, "accel") ||
                              has_word(cbuf, "accelera") || has_word(cbuf, "attrito") ||
                              has_word(cbuf, "friction") || has_word(cbuf, "frena");
            int c_has_grav = has_word(cbuf, "gravita") || has_word(cbuf, "gravity") ||
                             has_word(cbuf, "salto") || has_word(cbuf, "jump") ||
                             has_word(cbuf, "pavimento") || has_word(cbuf, "floor") ||
                             has_word(cbuf, "fermandosi");

            if (c_has_rect && c_has_accel && n_goals + 2 <= 16) {
                char gbuf[192], used_name[32];
                if (build_clause_goal(cbuf, entry_id, default_name,
                                      gbuf, sizeof(gbuf), used_name, sizeof(used_name)) == 0) {
                    snprintf(clauses[n_goals++], sizeof(clauses[0]), "%s", gbuf);
                    if (!have_default_name) {
                        snprintf(default_name, sizeof(default_name), "%s", used_name);
                        have_default_name = 1;
                    }
                    int acc = extract_int_near(cbuf, "accel", 1);
                    int fric = extract_int_near(cbuf, "attrit", -1);
                    if (fric < 0) fric = extract_int_near(cbuf, "fric", 1);
                    int maxv = extract_int_near(cbuf, "max", -1);
                    if (maxv < 0) maxv = extract_int_near(cbuf, "fino a", 3);
                    snprintf(clauses[n_goals++], sizeof(clauses[0]),
                             "(accel-x %s %d %d %d)", used_name, acc, fric, maxv);
                    added_accel_wire = 1;
                }
            } else if (wire_ctx && c_has_accel && !c_has_rect) {
                if (!added_accel_wire) {
                    int acc = extract_int_near(q, "accel", -1);
                    if (acc < 0) acc = 1;
                    int fric = extract_int_near(q, "attrit", -1);
                    if (fric < 0) fric = extract_int_near(q, "fric", 1);
                    int maxv = extract_int_near(q, "max", -1);
                    if (maxv < 0) maxv = extract_int_near(q, "fino a", 3);
                    snprintf(clauses[n_goals++], sizeof(clauses[0]),
                             "(accel-x %s %d %d %d)", default_name, acc, fric, maxv);
                    added_accel_wire = 1;
                }
            } else if (wire_ctx && has_grav_clause && c_has_grav) {
                if (!added_grav_wire) {
                    int grav = extract_int_near(q, "grav", 1);
                    int jv = extract_int_near(q, "salto", -1);
                    if (jv < 0) jv = extract_int_near(q, "salta di", -1);
                    if (jv < 0) jv = extract_int_near(q, "salta", -1);
                    if (jv < 0) jv = extract_int_near(q, "jump", 6);
                    int btn_v = extract_int_near(q, "tasto", -1);
                    if (btn_v < 0) btn_v = extract_int_near(q, "btn", 4);
                    int fy = extract_int_near(q, "pavimento y", -1);
                    if (fy < 0) fy = extract_int_near(q, "pavimento", -1);
                    if (fy < 0) fy = extract_int_near(q, "floor y", -1);
                    if (fy < 0) fy = extract_int_near(q, "fermandosi a", -1);
                    if (fy < 0) fy = extract_int_near(q, "floor", 180);
                    snprintf(clauses[n_goals++], sizeof(clauses[0]),
                             "(gravity-floor %s %d %d %d %d)", default_name, grav, jv, btn_v, fy);
                    added_grav_wire = 1;
                }
            } else {
                char gbuf[192];
                char used_name[32];
                if (build_clause_goal(cbuf, entry_id, default_name,
                                      gbuf, sizeof(gbuf), used_name, sizeof(used_name)) == 0) {
                    snprintf(clauses[n_goals++], sizeof(clauses[0]), "%s", gbuf);
                    if (!have_default_name) {
                        snprintf(default_name, sizeof(default_name), "%s", used_name);
                        have_default_name = 1;
                    }
                }
            }
        }
        cur = (*end) ? (end + sep_len) : end;
    }

    if (n_goals == 0) return -1;
    int any_wire = 0;
    for (int i = 0; i < n_goals; i++) {
        if (is_wire_part_goal(clauses[i])) { any_wire = 1; break; }
    }
    if (n_goals == 1 && !any_wire) {
        snprintf(goal, cap, "%s", clauses[0]);
        return 0;
    }
    size_t pos = 0;
    pos += (size_t)snprintf(goal + pos, cap - pos, "%s", any_wire ? "(wire-cart" : "(compose");
    for (int i = 0; i < n_goals && pos + 2 < cap; i++)
        pos += (size_t)snprintf(goal + pos, cap - pos, " %s", clauses[i]);
    if (pos + 2 <= cap) snprintf(goal + pos, cap - pos, ")");
    return 0;
}

int bm_synth_run(const char *query, const char *entry_id, const char *target,
                 char *code_out, size_t code_cap,
                 char *goal_out, size_t goal_cap,
                 char *err_out, size_t err_cap)
{
    if (!query || !query[0]) {
        if (err_out && err_cap) snprintf(err_out, err_cap, "synth: empty query");
        return -1;
    }
    if (ensure_vm(err_out, err_cap) != 0) return -1;
    bm_lisp_reset(synth_vm);

    const char *tgt = detect_target(query, target);
    const char *q = query;
    while (*q && isspace((unsigned char)*q)) q++;

    static char goal_buf[2048];
    static char eval_buf[4096];

    if (q[0] == '(') {
        snprintf(goal_buf, sizeof(goal_buf), "%s", q);
        if (strncmp(q, "(synth ", 7) == 0 || strncmp(q, "(solve-expr ", 12) == 0) {
            snprintf(eval_buf, sizeof(eval_buf), "%s", q);
        } else {
            snprintf(eval_buf, sizeof(eval_buf), "(synth '%s '%s)", tgt, q);
        }
    } else {
        if (build_goal_from_nl(q, entry_id, goal_buf, sizeof(goal_buf)) != 0) {
            if (err_out && err_cap) snprintf(err_out, err_cap, "no synthesis rule");
            return -1;
        }
        snprintf(eval_buf, sizeof(eval_buf), "(synth '%s '%s)", tgt, goal_buf);
    }

    if (goal_out && goal_cap) snprintf(goal_out, goal_cap, "%s", goal_buf);
    return bm_lisp_eval(synth_vm, eval_buf, code_out, code_cap, err_out, err_cap);
}
