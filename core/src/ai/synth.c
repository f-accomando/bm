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
    "    (_ (emit-stmt tgt \"\" ast))))\n";

/* Composable bm domain patterns -> Unified AST */
static const char *LISP_PATTERNS =
    "(define (sym-join a b) (str->sym (str-cat a \"_\" b)))\n"
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
    "    ((compose . ?goals)\n"
    "     (cons 'seq (map (lambda (x) (expand-goal tgt x)) ?goals)))\n"
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
        "boss", "ship", "nave", "slime", NULL
    };
    for (int i = 0; roles[i]; i++) {
        if (has_word(q, roles[i])) {
            snprintf(out, cap, "%s", roles[i]);
            return;
        }
    }
    snprintf(out, cap, "%s", (fallback && fallback[0]) ? fallback : "player");
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
    if (w < 0) w = extract_int_near(q, "w", 16);
    int h = extract_int_near(q, "alt", -1);
    if (h < 0) h = extract_int_near(q, "h", 16);
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
    int is_patrol = has_word(q, "patrol") || has_word(q, "pattuglia") || has_word(q, "chase") ||
                    has_word(q, "insegui");
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

static int build_goal_from_nl(const char *q, const char *entry_id, char *goal, size_t cap)
{
    char clauses[16][192];
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
        cur = (*end) ? (end + sep_len) : end;
    }

    if (n_goals == 0) return -1;
    if (n_goals == 1) {
        snprintf(goal, cap, "%s", clauses[0]);
        return 0;
    }
    size_t pos = 0;
    pos += (size_t)snprintf(goal + pos, cap - pos, "(compose");
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

    char goal_buf[2048];
    char eval_buf[4096];

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
