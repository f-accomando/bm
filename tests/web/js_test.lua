-- Host tests of the JavaScript engine of the bmweb cart (carts/bmweb/src/js.lua).
-- Pure Lua, no network. Run from the repository root:
--   /tmp/bmweb-luahost/luahost tests/web/js_test.lua
-- Build the host Lua first if it is missing:
--   mkdir -p /tmp/bmweb-luahost && cc -O2 -w -Ithird_party/lua -o /tmp/bmweb-luahost/luahost \
--     tests/kitchen/luahost.c third_party/lua/*.c -lm
package.path = "carts/bmweb/src/?.lua;" .. package.path

local js = require("js")

local checks, fails = 0, 0
local function check(cond, msg)
  checks = checks + 1
  if not cond then fails = fails + 1; print("FAIL: " .. msg) end
end
local function eq(got, want, msg)
  checks = checks + 1
  if got ~= want then
    fails = fails + 1
    print(string.format("FAIL: %s\n    got:  %s\n    want: %s", msg, tostring(got), tostring(want)))
  end
end

local rt = js.new()
local console = {}
rt.on_console = function(level, text) console[#console + 1] = level .. ":" .. text end

-- Run a script, return the last value as a string (or "ERR <message>").
local function ev(src, r)
  r = r or rt
  local ok, v = r:run(src, "t")
  if not ok then return "ERR " .. tostring(v) end
  if type(v) == "string" then return v end
  return r:to_string(v)
end
local function t(src, want, msg) eq(ev(src), want, msg or src) end
local function terr(src, want, msg)
  local got = ev(src)
  check(got:sub(1, 4) == "ERR " and got:find(want, 1, true) ~= nil, (msg or src) .. " -> " .. got .. " (want error " .. want .. ")")
end

---------------------------------------------------------------------------
-- numbers, strings, operators
---------------------------------------------------------------------------
t("1 + 2 * 3", "7")
t("(1 + 2) * 3", "9")
t("10 / 4", "2.5")
t("7 % 3", "1")
t("-7 % 3", "-1")
t("2 ** 10", "1024")
t("2 ** 3 ** 2", "512")
t("0.1 + 0.2", "0.30000000000000004")
t("1 / 3", "0.3333333333333333")
t("1e21", "1e+21")
t("123456789012345680000", "123456789012345680000")
t("1e-7", "1e-7")
t("0.000001", "0.000001")
t("-0", "0")
t("1 / 0", "Infinity")
t("-1 / 0", "-Infinity")
t("0 / 0", "NaN")
t("5e-324", "5e-324")
t("0xff + 0b101 + 0o17", "275")
t("1_000 + 1", "1001")
t("'a' + 1 + 2", "a12")
t("1 + 2 + 'a'", "3a")
t("'5' * '2'", "10")
t("'5' - 2", "3")
t("'abc' - 1", "NaN")
t("true + 1", "2")
t("null + 1", "1")
t("undefined + 1", "NaN")
t("[] + []", "")
t("[1,2] + [3]", "1,23")
t("({}) + ''", "[object Object]")
t("typeof 1 + typeof 'a' + typeof true + typeof undefined + typeof null + typeof {} + typeof (() => 1)",
  "numberstringbooleanundefinedobjectobjectfunction")
t("typeof notDefinedAnywhere", "undefined")
t("1 == '1'", "true")
t("0 == ''", "true")
t("null == undefined", "true")
t("null == 0", "false")
t("NaN == NaN", "false")
t("'1' === 1", "false")
t("[1] == 1", "true")
t("({}) == '[object Object]'", "true")
t("1 < 2 && 2 < 3", "true")
t("'b' > 'a'", "true")
t("'10' < '9'", "true")
t("10 < '9'", "false")
t("null >= 0", "true")
t("undefined == 0", "false")
t("5 & 3", "1")
t("5 | 3", "7")
t("5 ^ 3", "6")
t("~5", "-6")
t("1 << 31", "-2147483648")
t("-8 >> 1", "-4")
t("-8 >>> 28", "15")
t("4294967296 | 0", "0")
t("2147483648 | 0", "-2147483648")
t("1.9 | 0", "1")
t("var a = 5; a++ + ++a", "12")
t("var b = 1; b += 2; b *= 3; b -= 1; b /= 2; b %= 3; b **= 2; b", "1")
t("var c = null; c ??= 4; c ||= 9; c &&= 7; c", "7")
t("null ?? 'd'", "d")
t("0 ?? 'd'", "0")
t("0 || 'd'", "d")
t("1 && 2", "2")
t("0 && 2", "0")
t("(1, 2, 3)", "3")
t("void 0", "undefined")
t("true ? 'y' : 'n'", "y")
t("var o = {a: {b: null}}; String(o?.a?.b?.c) + String(o?.x?.y.z.w) + o.a?.['b']", "undefinedundefinednull")
t("var f = null; f?.(1)", "undefined")
t("var oo = {m() { return 5 }}; oo.m?.() + oo?.m()", "10")
t("`a${1 + 1}b${'c'}`", "a2bc")
t("`multi\nline`", "multi\nline")
t("'\\u0041\\x42\\n'.length", "3")
t("'tab\\there'", "tab\there")
t("'caf\\u00e9'", "caf\xc3\xa9")
t("// comment\n1 /* inline */ + 1", "2")
t("var x = 1\nvar y = 2\nx + y", "3")
t("var i = 0\ni\n++\ni\ni", "1")
t("function r() { return\n5 }\nString(r())", "undefined")
t("var s = 'a'\n;[1,2].length + s", "2a")
t("if (true) 1; else 2", "1")

---------------------------------------------------------------------------
-- variables, scope, closures
---------------------------------------------------------------------------
t("var v1 = typeof hoisted; var hoisted = 1; v1", "undefined")
t("hoistedFn()\nfunction hoistedFn() { return 'ok' }", "ok")
t("let l1 = 1; { let l1 = 2; } l1", "1")
t("var out = []; for (let i = 0; i < 3; i++) out.push(() => i); out.map(f => f()).join()", "0,1,2")
t("var out2 = []; for (var j = 0; j < 3; j++) out2.push(() => j); out2.map(f => f()).join()", "3,3,3")
t("function counter() { let n = 0; return () => ++n } var cn = counter(); cn(); cn(); cn()", "3")
t("(function () { const K = 1; try { K = 2 } catch (e) { return e instanceof TypeError } })()", "true")
t("const TOPK = 1; try { TOPK = 2; 'no' } catch (e) { e instanceof TypeError }", "true")
t("var g1 = 1; function sh() { var g1 = 2; return g1 } sh() + g1", "3")
t("(function () { var inner = 5; return typeof inner })() + typeof inner", "numberundefined")
t("window.zz = 9; zz", "9")
t("var topv = 3; window.topv + globalThis.topv + self.topv", "9")
t("function topf() {} typeof window.topf", "function")
t("this === window", "true")
t("var fe = function fact(n) { return n <= 1 ? 1 : n * fact(n - 1) }; fe(5)", "120")
t("(() => { try { return 1 } finally { 'x' } })()", "1")
t("{ function blockFn() { return 7 } blockFn() }", "7")
t("switch (3) { case 1: var sw = 'a'; break; default: sw = 'd'; case 2: sw += 'x' } sw", "dx")
t("var res = []; outer: for (var p = 0; p < 3; p++) { for (var q = 0; q < 3; q++) { if (q == 1) continue outer; if (p == 2) break outer; res.push(p + '' + q) } } res.join()", "00,10")
t("var w = 0, ww = 0; while (w < 5) { w++; if (w == 2) continue; if (w == 4) break; ww += w } ww", "4")
t("var d = 0; do { d++ } while (d < 3); d", "3")
t("var tot = 0; for (var k in {a: 1, b: 2, c: 3}) tot += k.length; tot", "3")
t("var keys = []; for (var k2 in [7, 8]) keys.push(k2); keys.join()", "0,1")
t("var chars = []; for (var ch of 'héy') chars.push(ch); chars.length", "3")
t("var sum = 0; for (const n of [1, 2, 3]) sum += n; sum", "6")
t("var ms = new Map([[1, 'a'], [2, 'b']]); var mo = []; for (const [k, v] of ms) mo.push(k + v); mo.join()", "1a,2b")
t("var ss = new Set([1, 1, 2]); var so = []; for (const v of ss) so.push(v); so.join()", "1,2")

---------------------------------------------------------------------------
-- functions
---------------------------------------------------------------------------
t("function dp(a, b = a + 1, c = 5) { return a + b + c } dp(1)", "8")
t("function dp2(a, b = 2) { return b } dp2(1, undefined) + dp2(1, null)", "2")
t("function rest(a, ...r) { return r.length + ':' + r.join('-') } rest(1, 2, 3, 4)", "3:2-3-4")
t("function sp(a, b, c) { return a + b + c } sp(...[1, 2], 3)", "6")
t("Math.max(...[1, 5, 3], 4)", "5")
t("[...'ab', ...[1, 2]].join()", "a,b,1,2")
t("({...{a: 1, b: 2}, b: 3, c: 4}).b", "3")
t("function ar() { return arguments.length + ':' + arguments[1] } ar(1, 'x', 3)", "3:x")
t("function ar2() { return Array.prototype.slice.call(arguments, 1).join() } ar2(1, 2, 3)", "2,3")
t("var arrow = (a, b) => a * b; arrow(3, 4)", "12")
t("var arrow2 = a => ({v: a}); arrow2(2).v", "2")
t("var arrow3 = () => { return 5 }; arrow3()", "5")
t("var objt = {n: 1, f() { return (() => this.n)() }}; objt.f()", "1")
t("var objt2 = {n: 1, f: function() { return this.n }}; var ff = objt2.f; String(ff.call({n: 9})) + objt2.f()", "91")
t("function ap(a, b) { return this.x + a + b } ap.apply({x: 1}, [2, 3]) + ap.call({x: 10}, 1, 1)", "18")
t("function bn(a, b) { return this.x + a + b } var bound = bn.bind({x: 1}, 2); bound(3)", "6")
t("(function () { return this === window })()", "true")
t("(function () { 'use strict'; return typeof this })()", "undefined")
t("class TC { m() { return typeof this } } var tcm = new TC().m; tcm()", "undefined")
t("function Cnt() { this.n = 0 } Cnt.prototype.inc = function () { return ++this.n }; var cc = new Cnt(); cc.inc(); cc.inc()", "2")
t("function P(n) { this.n = n } new P(4).n + '' + (new P(1) instanceof P)", "4true")
t("function Ret() { return {z: 1} } new Ret().z", "1")
t("(function f() { return f.name })()", "f")
t("(function (a, b) {}).length", "2")
t("var fn5 = function () {}; fn5.name", "fn5")
t("var cb = [3, 1, 2].sort((a, b) => b - a); cb.join()", "3,2,1")
t("((a, b) => a + b)(...[1, 2])", "3")
t("function sumAll() { var s = 0; for (var i = 0; i < arguments.length; i++) s += arguments[i]; return s } sumAll(1, 2, 3)", "6")
t("(function () { 'use strict'; return 1 })()", "1")
t("String(function foo() {})", "function foo() { [code] }")
t("String(Math.max)", "function max() { [native code] }")

---------------------------------------------------------------------------
-- objects, destructuring
---------------------------------------------------------------------------
t("var {a, b: {c = 5}, ...rest} = {a: 1, b: {}, d: 4, e: 5}; a + c + Object.keys(rest).join('')", "6de")
t("var [x, , y = 3, ...z] = [1, 2, undefined, 4, 5]; x + y + z.length", "6")
t("var m = 1, n = 2; [m, n] = [n, m]; m + '' + n", "21")
t("var oo2 = {}; [oo2.a, oo2['b']] = [1, 2]; oo2.a + oo2.b", "3")
t("var q1; ({q1} = {q1: 8}); q1", "8")
t("function pd({a, b = 2}, [c, d]) { return a + b + c + d } pd({a: 1}, [3, 4])", "10")
t("var key = 'dyn'; var oc = {[key + 1]: 1, ['x']: 2}; oc.dyn1 + oc.x", "3")
t("var sh = 5; ({sh}).sh", "5")
t("var og = {get v() { return 7 }, set v(x) { this._v = x }}; og.v = 3; og.v + og._v", "10")
t("var op = {a: 1}; delete op.a; 'a' in op", "false")
t("var ok = {b: 2, a: 1, 2: 'x', 1: 'y'}; Object.keys(ok).join()", "1,2,b,a")
t("Object.entries({a: 1, b: 2}).join(';')", "a,1;b,2")
t("Object.values({a: 1, b: 2}).join()", "1,2")
t("Object.assign({a: 1}, {b: 2}, null, {a: 3}).a", "3")
t("Object.fromEntries([['a', 1], ['b', 2]]).b", "2")
t("var fr = Object.freeze({a: 1}); fr.a = 2; fr.b = 3; fr.a + String(fr.b) + Object.isFrozen(fr)", "1undefinedtrue")
t("var oc2 = Object.create({inh: 1}); oc2.own = 2; oc2.inh + oc2.own + Object.keys(oc2).length", "4")
t("({a: 1}).hasOwnProperty('a') && !({}).hasOwnProperty('toString')", "true")
t("Object.getPrototypeOf([]) === Array.prototype", "true")
t("var dpv = {}; Object.defineProperty(dpv, 'x', {value: 1}); dpv.x = 2; dpv.x + Object.keys(dpv).length", "1")
t("var dpg = {}; Object.defineProperty(dpg, 'g', {get() { return 4 }, enumerable: true}); dpg.g + Object.keys(dpg).join()", "4g")
t("Object.getOwnPropertyNames({a: 1, b: 2}).join()", "a,b")
t("JSON.stringify(Object.getOwnPropertyDescriptor({a: 1}, 'a'))", '{"value":1,"writable":true,"enumerable":true,"configurable":true}')
t("var big = {}; for (var i = 0; i < 100; i++) big['k' + i] = i; delete big.k5; Object.keys(big).length + big.k99", "198")
t("Object.is(NaN, NaN) + '' + Object.is(0, -0)", "truefalse")
t("({}).toString() + Object.prototype.toString.call([]) + Object.prototype.toString.call(null)", "[object Object][object Array][object Null]")
t("Object.setPrototypeOf({}, null) instanceof Object", "false")
t("Object.hasOwn({a: 1}, 'a')", "true")

---------------------------------------------------------------------------
-- classes
---------------------------------------------------------------------------
t("class A { constructor(n) { this.n = n } get dbl() { return this.n * 2 } set dbl(v) { this.n = v / 2 } m() { return 'A' + this.n } static s() { return 'S' } } var a = new A(2); a.dbl = 10; a.dbl + a.m() + A.s()", "10A5S")
t("class B { m() { return 'B' } } class C extends B { m() { return super.m() + 'C' } } new C().m()", "BC")
t("class B2 { constructor(x) { this.x = x } } class C2 extends B2 { constructor() { super(5); this.y = 1 } } var c = new C2(); c.x + c.y", "6")
t("class D { } new D() instanceof D", "true")
t("class E1 extends Error { constructor(m) { super(m); this.name = 'E1' } } try { throw new E1('boom') } catch (e) { e.name + ':' + e.message + (e instanceof Error) + (e instanceof E1) }", "E1:boomtruetrue")
t("class F { static count = 3; x = 5; y = this.x + 1; #priv = 7; getP() { return this.#priv } } new F().y + F.count + new F().getP()", "16")
t("class G { static make() { return new this() } } G.make() instanceof G", "true")
t("class H { constructor() { return {custom: 1} } } new H().custom", "1")
t("class Stack extends Array { top() { return this[this.length - 1] } } var st = new Stack(); st.push(1, 2); st.top() + ':' + st.length", "2:2")
t("class I { get [Symbol.iterator]() { return null } } 1", "1")
t("class It { constructor() { this.d = [1, 2, 3] } [Symbol.iterator]() { var i = 0, d = this.d; return {next: () => i < d.length ? {value: d[i++], done: false} : {value: undefined, done: true}} } } [...new It()].join()", "1,2,3")
t("class Q { m() { return typeof Q } } new Q().m()", "function")
t("var Anon = class { v() { return 1 } }; new Anon().v()", "1")
t("class J { static #c = 0; static inc() { return ++J.#c } } J.inc(); J.inc()", "2")
terr("class K {} K()", "cannot be invoked without 'new'")
t("class L extends Object { constructor() { super(); this.q = 1 } } new L().q", "1")
t("class M1 { toString() { return 'M1!' } } `${new M1()}` + new M1()", "M1!M1!")
t("var o1 = {__proto__: {inherited: 1}, own: 2}; o1.inherited + o1.own", "3")
t("var sup = {hi() { return 'sup' }}; var sub = {__proto__: sup, hi() { return super.hi() + '+' }}; sub.hi()", "sup+")

---------------------------------------------------------------------------
-- errors and try/catch
---------------------------------------------------------------------------
t("try { null.x } catch (e) { e instanceof TypeError }", "true")
t("try { undefinedVar } catch (e) { e.name + ':' + e.message }", "ReferenceError:undefinedVar is not defined")
t("try { (void 0)() } catch (e) { e.constructor === TypeError }", "true")
t("try { throw 'str' } catch (e) { e }", "str")
t("try { throw {code: 5} } catch ({code}) { code }", "5")
t("var log = []; try { try { throw 1 } finally { log.push('f') } } catch (e) { log.push('c' + e) } log.join()", "f,c1")
t("function fin() { try { return 'a' } finally { return 'b' } } fin()", "b")
t("function fin2() { for (var i = 0; i < 3; i++) { try { continue } finally { if (i == 1) return 'ret' + i } } } fin2()", "ret1")
t("new Error('m').message + new TypeError('t').name + String(new RangeError('r'))", "mTypeErrorRangeError: r")
t("var e = new Error('x'); e.stack.indexOf('Error: x') >= 0", "true")
t("Error('call').message", "call")
t("try { null.foo() } catch (e) { e.message }", "Cannot read properties of null (reading 'foo')")
t("try { var u; u.bar = 1 } catch (e) { e.message }", "Cannot set properties of undefined (setting 'bar')")
t("try { ({}).foo() } catch (e) { e.message }", "(intermediate value).foo is not a function")
t("try { var obj = {}; obj.method() } catch (e) { e.message }", "obj.method is not a function")
t("try { new (void 0) } catch (e) { e.name }", "TypeError")
t("try { JSON.parse('{bad') } catch (e) { e.name }", "SyntaxError")
t("try { qq.y.z } catch (e) { e.name } ", "ReferenceError")
t("try { decodeURIComponent('%') } catch (e) { e.name }", "URIError")
t("try { [].reduce((a, b) => a) } catch (e) { e.name }", "TypeError")
t("try { new Array(-1) } catch (e) { e.name }", "RangeError")
t("Error.captureStackTrace({}); 'ok'", "ok")
t("var ec = new Error('with cause', {cause: 'c'}); ec.cause", "c")

---------------------------------------------------------------------------
-- strings
---------------------------------------------------------------------------
t("'abc'.length + 'abc'[1] + 'abc'.charAt(2) + 'abc'.charCodeAt(0)", "3bc97")
t("'Hello'.toUpperCase() + 'Hello'.toLowerCase()", "HELLOhello")
t("'  pad '.trim() + '|' + ' x '.trimStart() + '|' + ' x '.trimEnd() + '|'", "pad|x | x|")
t("'a,b,c'.split(',').join('-')", "a-b-c")
t("'abc'.split('').join('|')", "a|b|c")
t("'a1b22c'.split(/\\d+/).join('|')", "a|b|c")
t("'a1b2'.split(/(\\d)/).join('|')", "a|1|b|2|")
t("'a,b,c'.split(',', 2).join('|')", "a|b")
t("'abcabc'.indexOf('c') + ':' + 'abcabc'.lastIndexOf('c') + ':' + 'abc'.indexOf('z')", "2:5:-1")
t("'abc'.includes('bc') + '' + 'abc'.startsWith('ab') + 'abc'.endsWith('bc') + 'abc'.startsWith('b', 1)", "truetruetruetrue")
t("'abcdef'.slice(1, 3) + 'abcdef'.slice(-2) + 'abcdef'.substring(4, 2) + 'abcdef'.substr(1, 2)", "bcefcdbc")
t("'ab'.repeat(3) + '5'.padStart(3, '0') + '5'.padEnd(3, '-')", "ababab005" .. "5--")
t("'aXbX'.replace('X', '-') + 'aXbX'.replaceAll('X', '-')", "a-bXa-b-")
t("'john smith'.replace(/(\\w+)\\s(\\w+)/, '$2, $1')", "smith, john")
t("'abc'.replace(/b/, '[$&]') + 'abc'.replace(/b/, \"$`|$'\")", "a[b]ca" .. "a" .. "|c" .. "c")
t("'a-b-c'.replace(/-/g, (m, off) => off)", "a1b3c")
t("'x1y2'.replace(/(?<d>\\d)/g, '<$<d>>')", "x<1>y<2>")
t("'aaa'.replace(/a/g, 'b')", "bbb")
t("'abc'.at(-1) + 'abc'.concat('d', 1)", "cabcd1")
t("'b'.localeCompare('a') + 'a'.localeCompare('b') + 'a'.localeCompare('a')", "0")
t("String.fromCharCode(72, 105)", "Hi")
t("'\\u00e9'.charCodeAt(0)", "233")
t("'a\\nb'.split('\\n').length", "2")
t("'abc'.codePointAt(1)", "98")
t("'%E4'.length + encodeURIComponent('a b&c/d') + decodeURIComponent('%E2%82%AC')", "3a%20b%26c%2Fd\xe2\x82\xac")
t("encodeURI('http://x.com/a b?q=1&r=é')", "http://x.com/a%20b?q=1&r=%C3%A9")
t("'a-b'.split('-', 0).length", "0")
t("'The Quick'.match(/quick/i)[0]", "Quick")
t("'a1b2c3'.match(/\\d/g).join()", "1,2,3")
t("String('a1'.match(/x/))", "null")
t("'a1b2'.search(/\\d/)", "1")
t("[...'a1b2'.matchAll(/\\d/g)].map(m => m[0] + m.index).join()", "11,23")
t("'abc'.toString() + String(12) + String(null) + String(undefined) + String(true) + String([1, [2, 3]])", "abc12nullundefinedtrue1,2,3")
t("`${{toString() { return 'ts' }}}`", "ts")
t("'x'.padStart(5).length", "5")
t("'Ünï'.toUpperCase().length", "5")

---------------------------------------------------------------------------
-- regex engine
---------------------------------------------------------------------------
t("/a.c/.test('abc')", "true")
t("/^abc$/.test('abc') + '' + /^abc$/.test('abcd')", "truefalse")
t("/a|b|c/.exec('xxc')[0]", "c")
t("/(\\d+)-(\\d+)/.exec('tel 12-345')[2]", "345")
t("/a{2,3}/.exec('aaaa')[0]", "aaa")
t("/a+?/.exec('aaa')[0]", "a")
t("/a*?b/.exec('aaab')[0]", "aaab")
t("/(a|ab)(c|bcd)(d*)/.exec('abcd').join('|')", "abcd|a|bcd|")
t("/[^a-c]+/.exec('abcxyzabc')[0]", "xyz")
t("/[\\d\\s]+/.exec('ab 12 3c')[0]", " 12 3")
t("/\\bfoo\\b/.test('a foo b') + '' + /\\bfoo\\b/.test('afoob')", "truefalse")
t("/(?:ab)+/.exec('ababab')[0]", "ababab")
t("/(a)\\1/.test('aa') + '' + /(a)\\1/.test('ab')", "truefalse")
t("/a(?=b)/.exec('ab')[0] + /a(?!b)/.test('ab')", "afalse")
t("/^\\w+@\\w+\\.com$/.test('me@site.com')", "true")
t("/(?<y>\\d{4})-(?<m>\\d\\d)/.exec('2024-05').groups.m", "05")
t("/\\u0041\\x42/.test('AB')", "true")
t("/./.exec('\\u00e9x')[0]", "\xc3\xa9")
t("/[\\u00e0-\\u00ff]+/.exec('x\\u00e9\\u00e8y')[0].length", "4")
t("/a.b/s.test('a\\nb') + '' + /a.b/.test('a\\nb')", "truefalse")
t("/^b/m.test('a\\nb') + '' + /^b/.test('a\\nb')", "truefalse")
t("var rg = /o/g; rg.test('foo'); var li = rg.lastIndex; rg.test('foo'); li + ':' + rg.lastIndex", "2:3")
t("var rg2 = /a/y; rg2.test('ba')", "false")
t("var m = /(\\d)(x)?/.exec('1'); m.length + ':' + m[2] + ':' + m.index + ':' + m.input", "3:undefined:0:1")
t("new RegExp('a+', 'gi').flags + new RegExp('xy').source", "gixy")
t("/a/gi.toString()", "/a/gi")
t("/[/]/.test('/')", "true")
t("var res = 'a1b2'.replace(/\\d/g, d => d * 2); res", "a2b4")
terr("/(a+)+b/.test('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac')", "step limit")
t("/\\s+$/.test('x  ')", "true")
t("'a b  c'.split(/\\s+/).length", "3")
t("/^[\\w.+-]+@[\\w-]+\\.[\\w.-]+$/.test('first.last+tag@mail-host.co.uk')", "true")
terr("new RegExp('(')", "Invalid regular expression")
terr("new RegExp('a', 'zz')", "Invalid flags")
t("/[a-z]+/i.exec('HeLLo World')[0]", "HeLLo")
t("'x'.match(/(y)?/)[1]", "undefined")
t("'aBc'.replace(/b/i, 'X')", "aXc")
t("/^(?:\\+|-)?\\d+$/.test('-42')", "true")
t("/\\//.test('a/b')", "true")

---------------------------------------------------------------------------
-- arrays
---------------------------------------------------------------------------
t("var a = [1, 2, 3]; a.push(4, 5); a.pop(); a.shift(); a.unshift(0); a.join()", "0,2,3,4")
t("[1, 2, 3, 4].slice(1, -1).join() + [1, 2, 3].slice().length", "2,33")
t("var sp = [1, 2, 3, 4, 5]; var rem = sp.splice(1, 2, 'a', 'b', 'c'); sp.join() + '|' + rem.join()", "1,a,b,c,4,5|2,3")
t("var sp2 = [1, 2, 3]; sp2.splice(1); sp2.join()", "1")
t("[1].concat([2, 3], 4, [[5]]).length", "5")
t("[3, 1, 2].sort().join() + [10, 9, 1].sort().join() + ['b', 'a'].sort().join()", "1,2,31,10,9a,b")
t("[3, 1, 2].reverse().join()", "2,1,3")
t("[1, 2, 3].indexOf(2) + [1, 2, 3].lastIndexOf(3) + [NaN].includes(NaN)", "4")
t("[1, 2, 3].map(x => x * 2).filter(x => x > 2).reduce((a, b) => a + b, 0)", "10")
t("[[1, 2], [3]].reduceRight((a, b) => a.concat(b)).join()", "3,1,2")
t("[1, 2, 3].find(x => x > 1) + [1, 2, 3].findIndex(x => x > 5) + [1, 2, 3].findLast(x => x < 3)", "3")
t("[1, 2, 3].some(x => x > 2) + '' + [1, 2, 3].every(x => x > 1)", "truefalse")
t("[1, [2, [3, [4]]]].flat(Infinity).join() + [[1], [2]].flat().length", "1,2,3,42")
t("[1, 2].flatMap(x => [x, x * 10]).join()", "1,10,2,20")
t("new Array(3).fill(0).join() + Array.from({length: 3}, (_, i) => i * i).join()", "0,0,00,1,4")
t("Array.from('abc').join() + Array.from(new Set([1, 1, 2])).join() + Array.of(7, 8).join()", "a,b,c1,27,8")
t("Array.isArray([]) + '' + Array.isArray({}) + Array.isArray('a')", "truefalsefalse")
t("[1, 2, 3].at(-1) + [1, 2, 3].at(0)", "4")
t("var ks = []; for (var [i, v] of ['a', 'b'].entries()) ks.push(i + v); ks.join()", "0a,1b")
t("[...[1, 2].keys()].join() + [...[7, 8].values()].join()", "0,17,8")
t("var ar = [1, 2, 3]; ar.length = 1; ar.join() + ar.length", "11")
t("var ar2 = []; ar2[3] = 'x'; ar2.length + ':' + ar2.join('-')", "4:---x")
t("var ar3 = [1, 2, 3]; ar3.forEach((v, i, arr) => { arr[i] = v * 2 }); ar3.join()", "2,4,6")
t("[5, 1, 10].sort((a, b) => a - b).join()", "1,5,10")
t("var st = [{k: 1, v: 'a'}, {k: 0, v: 'b'}, {k: 1, v: 'c'}, {k: 0, v: 'd'}].sort((x, y) => x.k - y.k).map(o => o.v).join(''); st", "bdac")
t("[undefined, 3, 1].sort().join()", "1,3,")
t("[1, 2, 3].toString() + [1, [2, 3]].join(';')", "1,2,31;2,3")
t("[,1].length + [1,,2].length", "5")
t("var big = []; for (var i = 0; i < 1000; i++) big.push(i); big.length + big[999]", "1999")
t("[1, 2, 3].fill(9, 1).join()", "1,9,9")
t("var cw = [1, 2, 3, 4, 5]; cw.copyWithin(0, 3); cw.join()", "4,5,3,4,5")
t("JSON.stringify([1, 'a', null, undefined, () => 1])", '[1,"a",null,null,null]')
t("typeof [].length + [].length", "number0")

---------------------------------------------------------------------------
-- Number, Math, globals
---------------------------------------------------------------------------
t("(255).toString(16) + (255).toString(2) + (0.5).toString(2) + (-10).toString(36)", "ff111111110.1-a")
t("(3.14159).toFixed(2) + (2.5).toFixed(0) + (1.005).toFixed(2) + (0).toFixed(1) + (-1.5).toFixed(0)", "3.1431.000.0-2")
t("(1234.5678).toFixed(1) + (0.000001234).toFixed(8)", "1234.60.00000123")
t("(123.456).toPrecision(4) + (0.00123).toPrecision(2) + (1234567).toPrecision(2)", "123.50.00121.2e+6")
t("(12345.6789).toLocaleString() + (1e6).toLocaleString()", "12,345.6791,000,000")
t("(1e21).toLocaleString().length > 0", "true")
t("Number('  12 ') + Number('') + Number('0x10') + Number('1e3') + Number(true)", "1029")
t("Number('abc')", "NaN")
t("Number.isInteger(5) + '' + Number.isInteger(5.5) + Number.isFinite('5') + Number.isNaN(NaN)", "truefalsefalsetrue")
t("parseInt('42px') + parseInt('0x1f') + parseInt('101', 2) + parseInt('  -7')", "71")
t("parseFloat('3.5abc') + parseFloat('.5') + parseFloat('-1e2')", "-96")
t("isNaN('x') + '' + isNaN('5') + isFinite('1e308') + isFinite(Infinity)", "truefalsetruefalse")
t("Number.MAX_SAFE_INTEGER + 1 === Number.MAX_SAFE_INTEGER + 2", "true")
t("Number.EPSILON > 0 && Number.EPSILON < 1e-10", "true")
t("Math.round(2.5) + Math.round(-2.5) + Math.round(2.4) + Math.floor(-1.5) + Math.ceil(1.2) + Math.trunc(-1.9)", "2")
t("Math.abs(-3) + Math.sign(-5) + Math.sqrt(16) + Math.cbrt(27) + Math.pow(2, 8)", "265")
t("Math.max() + Math.min()", "NaN")
t("Math.max(1, NaN) + '' + Math.min(3, 1, 2) + Math.max(1, 3, 2)", "NaN13")
t("Math.hypot(3, 4) + Math.atan2(0, 1) + Math.log10(1000) + Math.log2(8)", "11")
t("Math.PI.toFixed(5) + Math.E.toFixed(3)", "3.141592.718")
t("Math.clz32(1) + Math.imul(3, 4) + Math.fround(5)", "48")
t("var r = Math.random(); r >= 0 && r < 1", "true")
t("Math.exp(0) + Math.log(1) + Math.sin(0) + Math.cos(0)", "2")
t("Boolean('') + '' + Boolean('a') + Boolean(0) + !!NaN + !![] + !!{}", "falsetruefalsefalsetruetrue")
t("Symbol('d').toString() + Symbol('d').description + (Symbol('a') === Symbol('a'))", "Symbol(d)dfalse")
t("typeof Symbol.iterator + typeof Symbol()", "symbolsymbol")
t("var sy = Symbol('k'); var os = {[sy]: 1, a: 2}; os[sy] + Object.keys(os).join()", "1a")
t("structuredClone({a: [1, {b: 2}]}).a[1].b", "2")
t("var cyc = {n: 1}; cyc.self = cyc; var cl = structuredClone(cyc); cl.self === cl", "true")
t("globalThis === window && self === window", "true")
t("typeof Intl", "undefined")

---------------------------------------------------------------------------
-- JSON
---------------------------------------------------------------------------
t("JSON.stringify({a: 1, b: [1, 2, {c: null}], d: 'x\"y', e: undefined, f() {}})", '{"a":1,"b":[1,2,{"c":null}],"d":"x\\"y"}')
t("JSON.stringify({a: [1, 2], b: {}}, null, 2)", '{\n  "a": [\n    1,\n    2\n  ],\n  "b": {}\n}')
t("JSON.stringify([1], null, '--')", "[\n--1\n]")
t("JSON.stringify({a: 1, b: 2, c: 3}, ['a', 'c'])", '{"a":1,"c":3}')
t("JSON.stringify({a: 1, b: 2}, (k, v) => typeof v == 'number' ? v * 2 : v)", '{"a":2,"b":4}')
t("JSON.stringify({toJSON() { return 'custom' }})", '"custom"')
t("JSON.stringify(new Date(0))", '"1970-01-01T00:00:00.000Z"')
t("JSON.stringify('\\u0001\\n')", '"\\u0001\\n"')
t("JSON.stringify(NaN) + JSON.stringify(undefined) + JSON.stringify(null) + JSON.stringify(Infinity)", "nullundefinednullnull")
t("try { var cy = {}; cy.c = cy; JSON.stringify(cy) } catch (e) { e.name }", "TypeError")
t("JSON.parse('{\"a\": [1, 2.5e1, -3, true, false, null, \"s\\\\n\\\\u00e9\"]}').a.join()", "1,25,-3,true,false,,s\n\xc3\xa9")
t("JSON.parse(' [ ] ').length", "0")
t("JSON.parse('\"x\"')", "x")
t("JSON.parse('{\"__proto__\": 1}').__proto__", "1")
t("JSON.parse('{\"a\":{\"b\":2}}', (k, v) => typeof v == 'number' ? v + 1 : v).a.b", "3")
for _, bad in ipairs({ "{'a':1}", "[1,]", "{\"a\":1,}", "01", "1.", "\"abc", "tru", "[1 2]", "", "{\"a\" 1}", "\"\\x\"", "nul" }) do
  local got = ev("try { JSON.parse(" .. string.format("%q", bad):gsub("\\\n", "\\n") .. "); 'parsed' } catch (e) { e.name }")
  eq(got, "SyntaxError", "JSON.parse rejects " .. bad)
end
t("JSON.stringify(JSON.parse('{\"a\":[{\"b\":1}]}'))", '{"a":[{"b":1}]}')
t("JSON.stringify({date: new Date(86400000)})", '{"date":"1970-01-02T00:00:00.000Z"}')
t("JSON.stringify({s: new Set([1]), m: new Map()})", '{"s":{},"m":{}}')

---------------------------------------------------------------------------
-- Date
---------------------------------------------------------------------------
t("new Date(0).toISOString()", "1970-01-01T00:00:00.000Z")
t("new Date(2024, 1, 29, 12, 30, 15, 500).toISOString()", "2024-02-29T12:30:15.500Z")
t("new Date('2024-03-15T10:20:30Z').getTime()", "1710498030000")
t("new Date('2024-03-15').getDate() + new Date('2024-03-15').getMonth() + new Date('2024-03-15').getFullYear()", "2041")
t("new Date('2024-03-15T10:20:30+02:00').getUTCHours()", "8")
t("new Date(2020, 0, 31).getDay()", "5")
t("var d = new Date(0); d.setFullYear(2000); d.setMonth(11); d.setDate(25); d.setHours(8, 9, 10); d.toISOString()", "2000-12-25T08:09:10.000Z")
t("var d2 = new Date(2020, 0, 31); d2.setMonth(1); d2.toISOString().slice(0, 10)", "2020-03-02")
t("new Date(NaN).getTime() + '' + isNaN(new Date('garbage')) + String(new Date(NaN))", "NaNtrueInvalid Date")
t("Date.UTC(2000, 0, 1)", "946684800000")
t("typeof Date.now() + (Date.now() > 1e12)", "numbertrue")
t("new Date(5).getTime() + new Date(new Date(7)).getTime() + new Date(2000, 0).getHours()", "12")
t("new Date(86400000 * 365).getFullYear()", "1971")
t("new Date(-1).toISOString()", "1969-12-31T23:59:59.999Z")
t("new Date(0).toString()", "Thu Jan 01 1970 00:00:00 GMT+0000 (UTC)")
t("new Date(0).toUTCString()", "Thu, 01 Jan 1970 00:00:00 GMT")
t("new Date(1e12).toLocaleDateString() + ' ' + new Date(1e12).toLocaleTimeString()", "9/9/2001 1:46:40 AM")
t("new Date(2000, 5, 15) - new Date(2000, 5, 14)", "86400000")
t("new Date(0).getTimezoneOffset()", "0")
t("new Date('Tue, 03 Mar 2020 10:00:00 GMT').toISOString()", "2020-03-03T10:00:00.000Z")
t("new Date('March 3, 2020 10:00:00').toISOString()", "2020-03-03T10:00:00.000Z")
rt.now_ms = 1700000000000
t("Date.now() + new Date().getTime()", "3400000000000")
t("new Date() > new Date(0)", "true")
rt.now_ms = nil

---------------------------------------------------------------------------
-- Map, Set, WeakMap
---------------------------------------------------------------------------
t("var m = new Map(); m.set('a', 1).set('b', 2); m.size + m.get('a') + m.get('zz') + m.has('b')", "NaN")
t("var m1 = new Map([[NaN, 'n'], [0, 'z']]); m1.get(NaN) + m1.get(-0)", "nz")
t("var m2 = new Map(); var ko = {}; m2.set(ko, 'o'); m2.get(ko) + m2.get({})", "oundefined")
t("var m3 = new Map([[1, 'a'], [2, 'b'], [3, 'c']]); m3.delete(2); [...m3.keys()].join() + [...m3.values()].join() + [...m3.entries()].join(';')", "1,3a,c1,a;3,c")
t("var m4 = new Map([[1, 'a']]); var seen = []; m4.forEach((v, k) => seen.push(k + v)); m4.clear(); seen.join() + m4.size", "1a0")
t("var s = new Set([1, 2, 2, 3]); s.add(4); s.delete(1); s.size + [...s].join()", "32,3,4")
t("new Set('hello').size", "4")
t("var s2 = new Set([NaN, NaN, 0, -0]); s2.size", "2")
t("var s3 = new Set([1]); var it = s3.values(); var r1 = it.next(); var r2 = it.next(); r1.value + ':' + r1.done + ':' + r2.done", "1:false:true")
t("var wm = new WeakMap(); var k = {}; wm.set(k, 5); wm.get(k) + wm.has(k) + wm.has({})", "6")
t("var ws = new WeakSet(); var wk = {}; ws.add(wk); ws.has(wk) + '' + ws.has({})", "truefalse")
terr("new WeakMap().set(1, 2)", "Invalid value used as weak map key")
terr("Map()", "requires 'new'")
t("var mm = new Map(); for (var i = 0; i < 200; i++) { mm.set(i, i) } for (var j = 0; j < 190; j++) mm.delete(j); mm.set('x', 1); mm.size + [...mm.keys()].slice(0, 2).join()", "11190,191")
t("Object.prototype.toString.call(new Map()) + typeof new Set().size", "[object Map]number")

---------------------------------------------------------------------------
-- Promise, timers, microtasks
---------------------------------------------------------------------------
do
  local r = js.new()
  r.now_ms = 1000
  local out = {}
  r:set(r.global, "emit", r:native(function(this, v) out[#out + 1] = r:to_string(v) end, "emit"))
  local ok = r:run([[
    emit('sync1');
    Promise.resolve(1).then(v => emit('then' + v));
    queueMicrotask(() => emit('micro'));
    setTimeout(() => emit('timeout'), 10);
    setTimeout((a, b) => emit('args' + a + b), 5, 'x', 'y');
    new Promise((res, rej) => rej(new Error('bad'))).catch(e => emit('caught ' + e.message)).finally(() => emit('fin'));
    emit('sync2');
  ]], "p")
  check(ok, "promise script runs")
  eq(table.concat(out, ","), "sync1,sync2,then1,micro,caught bad,fin", "microtask order after run")
  eq(r:next_timer(), 1005, "next timer is the earliest")
  eq(r:tick(1004), 0, "no timer due yet")
  eq(r:tick(1007), 1, "one timer due")
  eq(out[#out], "argsxy", "timer args")
  eq(r:tick(1010), 1, "second timer")
  eq(out[#out], "timeout", "timer order")
  eq(r:next_timer(), nil, "no timers left")

  out = {}
  r:run([[
    var iv = 0, id = setInterval(() => { emit('iv' + (++iv)); if (iv == 3) clearInterval(id) }, 20);
    var cleared = setTimeout(() => emit('never'), 1); clearTimeout(cleared);
    requestAnimationFrame(ts => emit('raf' + typeof ts));
    Promise.all([1, Promise.resolve(2), new Promise(r => setTimeout(() => r(3), 30))]).then(v => emit('all' + v.join('')));
    Promise.race([new Promise(r => setTimeout(() => r('slow'), 50)), 'fast']).then(v => emit('race' + v));
    Promise.allSettled([Promise.reject('no'), 1]).then(v => emit('as' + v.map(x => x.status).join('/')));
    Promise.any([Promise.reject(1), Promise.resolve(9)]).then(v => emit('any' + v));
    (async_missing = 1);
  ]], "p2")
  r.now_ms = 1020
  r:tick(1020)
  r:tick(1040); r:tick(1060); r:tick(1100)
  local joined = table.concat(out, ",")
  local i1, i2, i3 = joined:find("iv1", 1, true), joined:find("iv2", 1, true), joined:find("iv3", 1, true)
  check(i1 and i2 and i3 and i1 < i2 and i2 < i3 and not joined:find("iv4") and not joined:find("never") and joined:find("rafnumber", 1, true),
    "interval and raf: " .. joined)
  check(joined:find("all123", 1, true), "Promise.all: " .. joined)
  check(joined:find("racefast", 1, true), "Promise.race: " .. joined)
  check(joined:find("asrejected/fulfilled", 1, true), "Promise.allSettled: " .. joined)
  check(joined:find("any9", 1, true), "Promise.any: " .. joined)

  out = {}
  r:run([[
    var p = new Promise(res => res(Promise.resolve('inner')));
    p.then(v => { emit(v); return {then(ok) { ok('thenable') }} }).then(v => emit(v));
    Promise.reject(new Error('x')).then(() => emit('no')).catch(e => emit('c:' + e.message));
    Promise.resolve().then(() => { throw new TypeError('thrown') }).catch(e => emit(e.name));
  ]], "p3")
  eq(table.concat(out, ","), "c:x,TypeError,inner,thenable", "promise chaining")
  eq(ev("typeof Promise.resolve(1).then", r), "function", "promise methods")

  -- timers: limits
  local many = r:run("for (var i = 0; i < 300; i++) setTimeout(function () {}, 1000)", "many")
  check(not many, "more than 200 timers is an error")
  local r2 = js.new()
  r2.now_ms = 0
  r2:run("var n = 0; for (var i = 0; i < 80; i++) setTimeout(function () { n++ }, 1); setInterval(function(){ n += 1000 }, 1)", "t")
  eq(r2:tick(10), 50, "tick runs at most 50 timers")
  eq(r2:tick(10), 31, "the rest on the next tick")
  eq(r2:next_timer(), 14, "interval re-arms at now + max(ms, 4)")
end

---------------------------------------------------------------------------
-- run(), errors, budgets, limits
---------------------------------------------------------------------------
do
  local r = js.new({ budget = 100000 })
  local ok, msg = r:run("while (true) {}", "loop.js")
  check(not ok and msg:find("script too slow (step limit)", 1, true) and msg:find("loop.js:", 1, true), "infinite loop: " .. tostring(msg))
  ok, msg = r:run("try { while (true) {} } catch (e) { 'caught' }", "loop2")
  check(not ok and msg:find("step limit", 1, true), "step limit is not catchable: " .. tostring(msg))
  ok, msg = r:run("function f() { return f() } try { f() } catch (e) { e instanceof RangeError ? 'range' : 'other' }", "rec")
  check(ok and msg == "range", "stack overflow is a catchable RangeError: " .. tostring(msg))
  ok, msg = r:run("function f(n) { return f(n + 1) } f(0)", "rec2")
  check(not ok and msg:find("RangeError: Maximum call stack size exceeded", 1, true), "uncaught overflow: " .. tostring(msg))
  ok, msg = r:run("1 +* 2", "syn.js")
  check(not ok and msg:find("SyntaxError", 1, true) and msg:find("(syn.js:1)", 1, true), "syntax error: " .. tostring(msg))
  ok, msg = r:run("var a = 1;\nvar b = 2;\nfoo(", "syn2")
  check(not ok and msg:find("SyntaxError", 1, true) and msg:find("syn2:3", 1, true), "syntax error line: " .. tostring(msg))
  ok, msg = r:run("var a = 1;\n\nnull.x", "rt.js")
  check(not ok and msg:find("TypeError: Cannot read properties of null (reading 'x') (rt.js:3)", 1, true), "runtime error with line: " .. tostring(msg))
  ok, msg = r:run("var o = {};\no.f()", "rt2")
  eq(msg, "TypeError: o.f is not a function (rt2:2)", "not a function message")
  ok, msg = r:run("throw new Error('custom')", "th")
  eq(msg, "Error: custom (th:1)", "uncaught throw")
  ok, msg = r:run("throw 'plain'", "th2")
  eq(msg, "Uncaught plain (th2:1)", "uncaught non-error throw")
  ok, msg = r:run("undefinedThing", "ref")
  eq(msg, "ReferenceError: undefinedThing is not defined (ref:1)", "reference error")
  -- budget resets on each run
  for i = 1, 3 do
    ok = r:run("var x = 0; for (var i = 0; i < 20000; i++) x += i; x", "reset")
    check(ok, "budget resets at each run (" .. i .. ")")
  end
  ok, msg = r:run("var s = 'x'; while (true) s += s", "bigstr")
  check(not ok and msg:find("RangeError: Invalid string length", 1, true), "string size cap: " .. tostring(msg))
  ok, msg = r:run("var a = []; while (true) a.push(1)", "bigarr")
  check(not ok and (msg:find("Invalid array length", 1, true) or msg:find("step limit", 1, true)), "array growth cap: " .. tostring(msg))
  ok, msg = r:run("new Array(2e9)", "bigarr2")
  check(not ok and msg:find("RangeError", 1, true), "huge array: " .. tostring(msg))
  ok, msg = r:run("'x'.repeat(1e9)", "rep")
  check(not ok and msg:find("RangeError", 1, true), "repeat cap: " .. tostring(msg))
  ok, msg = r:run(string.rep("[", 300) .. string.rep("]", 300), "deep")
  check(not ok and msg:find("SyntaxError", 1, true), "deep nesting is a SyntaxError: " .. tostring(msg))
  ok, msg = r:run(string.rep("(", 5000) .. "1" .. string.rep(")", 5000), "deep2")
  check(not ok and msg:find("SyntaxError", 1, true), "deep parens: " .. tostring(msg))
  ok, msg = r:run("var x = '" .. string.rep("a", 600000) .. "'", "huge")
  check(not ok and msg:find("SyntaxError", 1, true), "source size cap: " .. tostring(msg))
  ok, msg = r:run("/(a+)+b/.test('" .. string.rep("a", 40) .. "c')", "catastrophic")
  check(not ok and msg:find("step limit", 1, true), "catastrophic regex: " .. tostring(msg))
  ok, msg = r:run("JSON.parse('" .. string.rep("[", 300) .. "')", "jdeep")
  check(not ok, "deep JSON: " .. tostring(msg))
  for _, src in ipairs({ "async function f() {}", "var f = async () => 1", "await 1", "function* g() {}", "with (x) {}", "import x from 'y'",
    "export var a", "eval('1')", "new Function('return 1')", "a`tag`" }) do
    ok, msg = r:run(src, "unsup")
    check(not ok and (msg:find("SyntaxError", 1, true) or msg:find("EvalError", 1, true)), "unsupported syntax gives an error: " .. src .. " -> " .. tostring(msg))
  end
  ok, msg = r:run("await foo", "aw")
  check(msg:find("await is not supported", 1, true), "await message: " .. tostring(msg))
end

---------------------------------------------------------------------------
-- host API
---------------------------------------------------------------------------
do
  local r = js.new()
  local logs = {}
  r.on_console = function(level, text) logs[#logs + 1] = level .. ":" .. text end
  -- natives and globals
  local doc = r:new_object()
  r:set(r.global, "document", doc)
  r:set(doc, "title", "T")
  r:set(doc, "getEl", r:native(function(this, id)
    local el = r:new_object()
    el.host = { id = id, text = "initial" }
    r:define_accessor(el, "textContent", function(o) return o.host.text end, function(o, v) o.host.text = r:to_string(v) end)
    r:set(el, "tag", "DIV")
    return el
  end, "getEl", 1))
  check(r:run("var e = document.getEl('x'); e.textContent += '!'; e.textContent = e.textContent + '?'; document.title", "h"), "host script")
  eq(ev("e.textContent", r), "initial!?", "accessor getter and setter")
  eq(ev("Object.keys(e).join()", r), "tag", "accessor is not enumerable")
  eq(ev("document.title + typeof document + (window.document === document)", r), "Tobjecttrue", "global object property")
  -- native calls
  local f = r:native(function(this, a, b) return (a or 0) + (b or 0) end, "adder", 2)
  eq(r:call(f, nil, 2, 3), 5, "rt:call native")
  check(math.type(r:call(f, nil, 2.0, 3.0)) == "float", "numbers stay floats")
  local jsf = (select(2, r:run("(function (a, b) { return a * b + (this && this.k || 0) })", "f")))
  eq(r:call(jsf, nil, 3, 4), 12, "rt:call JS function")
  eq(r:call(jsf, r:new_object(), 3, 4), 12, "rt:call JS function with this")
  local ok, v = r:call_safe(jsf, nil, 2, 5)
  check(ok and v == 10, "call_safe ok")
  ok, v = r:call_safe(r:native(function() error("lua boom") end, "bad"))
  check(not ok and v:find("lua boom", 1, true) and not v:find("stack traceback", 1, true), "call_safe converts Lua errors: " .. tostring(v))
  ok, v = r:call_safe((select(2, r:run("(function () { null.x })", "f"))))
  check(not ok and v:find("TypeError", 1, true), "call_safe JS error: " .. tostring(v))
  ok, v = r:call_safe(5)
  check(not ok and v:find("not a function", 1, true), "call_safe on a non-function: " .. tostring(v))
  ok, v = r:run("var guard = document.noSuch(); 1", "g")
  check(not ok, "a script cannot escape through a missing host function")
  ok, v = r:run("(function () { try { (function () { __luaerr__() })() } catch (e) { return 'c:' + e.name } })()", "g2")
  check(ok and v == "c:ReferenceError", "undefined global is a ReferenceError")
  r:set(r.global, "luaerr", r:native(function() error("native failure") end, "luaerr"))
  eq(ev("try { luaerr() } catch (e) { e.message }", r), "native failure", "Lua errors in natives are catchable JS errors")
  local ok2, m2 = r:run("luaerr()", "n")
  check(not ok2 and m2:find("native failure", 1, true), "Lua error in native: " .. tostring(m2))
  -- thrown values
  local thrower = r:native(function() error(setmetatable({ value = "thrown!" }, js.Throw)) end, "thrower")
  r:set(r.global, "thrower", thrower)
  eq(ev("try { thrower() } catch (e) { e }", r), "thrown!", "host throws a JS value")
  -- construct, arrays, keys
  local arr = r:new_array({ 1.0, "two", 3.0 })
  r:set(r.global, "harr", arr)
  eq(ev("harr.length + harr.map(String).join('-')", r), "31-two-3", "host array")
  local list, n = r:array_to_list(arr)
  check(n == 3 and list[2] == "two", "array_to_list")
  local o = r:new_object()
  r:set(o, "z", 1.0); r:set(o, "a", 2.0); r:set(o, 5.0, "five"); r:set(o, "b", 3.0)
  local ks, kn = r:keys(o)
  eq(table.concat(ks, ",", 1, kn), "5,z,a,b", "rt:keys order: integer keys first")
  check(r:is_array(arr) and not r:is_array(o), "is_array")
  check(r:is_callable(f) and not r:is_callable(o), "is_callable")
  eq(r:typeof(f) .. r:typeof(o) .. r:typeof(nil) .. r:typeof(js.null) .. r:typeof(1.0), "functionobjectundefinedobjectnumber", "typeof")
  eq(r:to_string(1.5), "1.5", "to_string")
  eq(r:to_number("12"), 12, "to_number")
  check(r:to_boolean("x") and not r:to_boolean(0.0), "to_boolean")
  local Ctor = (select(2, r:run("(function Pt(x) { this.x = x })", "c")))
  local inst = r:construct(Ctor, 7)
  eq(r:get(inst, "x"), 7, "rt:construct")
  eq(ev("harr", r), "1,two,3", "array toString")
  -- the host field is free
  inst.host = { secret = 1 }
  eq(ev("1", r), "1", "host field untouched")
  -- the null sentinel
  check(js.null ~= nil and r:run("var nn = null", "n") and r:get(r.global, "nn") == js.null, "null sentinel")
  check(r:get(r.global, "undefinedVar") == nil, "undefined is nil")
  -- console
  r:run("console.log('a', 1, [1, 'x'], {k: 'v', n: {m: {d: {e: 1}}}}, null, undefined); console.warn('w'); console.error('e'); console.info(true); console.debug('d')", "c")
  eq(logs[1], "log:a 1 [1, 'x'] {k: 'v', n: {m: {d: [Object]}}} null undefined", "console.log formatting")
  eq(logs[2], "warn:w", "console.warn level")
  eq(logs[3], "error:e", "console.error level")
  eq(logs[4], "info:true", "console.info level")
  eq(logs[5], "debug:d", "console.debug level")
  logs = {}
  r:run("console.log('x'.repeat(500))", "c2")
  check(#logs[1] <= 210, "console text is limited")
  r:run("console.log(new Error('E'), function foo() {}, class Z {}, new Map([[1, 2]]), new Set([1]), /re/g, [1, [2, [3, [4]]]])", "c3")
  eq(logs[#logs], "log:Error: E [Function: foo] [class Z] Map(1) {1 => 2} Set(1) {1} /re/g [1, [2, [3, [Array]]]]", "console inspect kinds")
  -- sandbox
  for _, name in ipairs({ "os", "io", "require", "load", "loadstring", "dofile", "debug", "string", "table", "math", "package", "_G", "_ENV", "collectgarbage", "setmetatable", "coroutine", "utf8" }) do
    eq(ev("typeof " .. name, r), "undefined", "no Lua global: " .. name)
  end
  eq(ev("typeof Math + typeof JSON + typeof String", r), "objectobjectfunction", "JS globals present")
  eq(ev("Object.keys(window).includes('os') + ''", r), "false", "global object has no Lua names")
  local before = {}
  for k in pairs(_G) do before[k] = true end
  r:run("var leak = 1; globalThis.leak2 = 2; function leak3() {}", "leak")
  local leaked = false
  for k in pairs(_G) do if not before[k] then leaked = true end end
  check(not leaked, "scripts do not write Lua globals")
  -- two runtimes are independent
  local ra, rb = js.new(), js.new()
  ra:run("var shared = 'A'", "a")
  rb:run("var shared = 'B'", "b")
  eq(ev("shared", ra), "A", "runtime A state")
  eq(ev("shared", rb), "B", "runtime B state")
  eq(ev("typeof shared", rt), "undefined", "other runtime untouched")
  -- value contract
  local _, num = r:run("1 + 1", "n")
  check(math.type(num) == "float", "numbers are floats")
  local _, s2 = r:run("'a' + 'b'", "n")
  eq(s2, "ab", "strings are Lua strings")
  local _, bo = r:run("1 < 2", "n")
  eq(bo, true, "booleans are Lua booleans")
  local _, un = r:run("void 0", "n")
  eq(un, nil, "undefined is nil")
  local _, nl = r:run("null", "n")
  eq(nl, js.null, "null is js.null")
  for _, src in ipairs({ "[1,2,3].length", "'abc'.length", "'abc'.indexOf('c')", "[3,1].indexOf(1)", "'a'.charCodeAt(0)", "Math.floor(2.5)",
    "Math.round(2.5)", "Math.ceil(2.1)", "parseInt('5')", "new Date(0).getTime()", "new Date(1e12).getFullYear()", "Math.trunc(5.5)",
    "[1,2].push(1)", "new Map().size", "'abc'.search(/c/)", "Math.sign(3)", "Math.abs(-4)", "Math.max(1,2)", "Number('7')", "(5).valueOf()",
    "Math.hypot(3,4)", "Math.imul(2,3)", "'ab'.lastIndexOf('b')", "[1].findIndex(x => x)", "Date.now()", "Math.clz32(8)" }) do
    local _, v = r:run(src, "n")
    check(math.type(v) == "float", "float result: " .. src .. " -> " .. tostring(math.type(v)))
  end
  -- budget reaches host calls and nested run
  local r3 = js.new({ budget = 5000 })
  local ok3, m3 = r3:run("var t = 0; for (;;) { t++ }", "b")
  check(not ok3 and m3:find("step limit", 1, true), "custom budget")
  r3.budget = 10
  ok3, m3 = r3:run("for (var i = 0; i < 100; i++) {}", "b2")
  check(not ok3, "rt.budget is read at each run")
  r3.steps_left = 0
  check(type(r3.steps_left) == "number", "rt.steps_left is a number")
end

---------------------------------------------------------------------------
-- parser corners
---------------------------------------------------------------------------
t("var a = 1; var b = a / 2 / 1; b", "0.5")
t("var re = 4 / 2; var x = /2/.test('2'); re + '' + x", "2true")
t("var s = 'x'; s\n.toUpperCase()\n.toLowerCase()", "x")
t("var o = {if: 1, class: 2, new: 3}; o.if + o.class + o.new", "6")
t("var o2 = {'quoted-key': 1, 42: 2}; o2['quoted-key'] + o2[42]", "3")
t("var f = function () { return 1 }\n;(function () { return 2 })()", "2")
t("var fn = x => x * 2, fn2 = (x, y) => x + y; fn(2) + fn2(1, 2)", "7")
t("var g = (a, b = (c) => c * 2) => b(a); g(4)", "8")
t("[1, 2, 3].map((x) => { return x * 2 }).join()", "2,4,6")
t("var {a: {b: [, second]}} = {a: {b: [1, 2]}}; second", "2")
t("var i = 10; i-- - --i", "2")
t("var x = 5; x = x++ + 1; x", "6")
t("1 + +'2' + -'1'", "2")
t("typeof typeof 1", "string")
t("!function () { return 0 }()", "true")
t("new Date(0).getTime.call(new Date(5))", "5")
t("new (class { constructor() { this.v = 3 } })().v", "3")
t("new Array(1, 2).length + new Object().constructor.name", "2Object")
t("var a = [1, 2, 3], [h, ...tl] = a; h + tl.join('')", "123")
t("'use strict'; var useStrict = 1; useStrict", "1")
t("debugger; 5", "5")
t("var $ = 1, _ = 2, ab = 3; $ + _ + ab", "6")
t("x = 5; x", "5")
t("var fnexpr = function () {}; typeof fnexpr", "function")
t("if (1) { var inIf = 1 } else { var inElse = 2 } typeof inIf + typeof inElse", "numberundefined")
t("a: { var after = 1; break a; after = 2 } after", "1")
t("var n = 0; for (;;) { if (++n > 3) break } n", "4")
t("for (var i = 0, j = 10; i < j; i += 4, j -= 4); i + ':' + j", "8:2")
t("var obj = {a: 1, b: 2}; var sum = 0; for (let key in obj) { sum += obj[key] } sum", "3")
t("(function () { return [typeof arguments, arguments.length] })(1, 2).join()", "object,2")
t("'\\'' + \"\\\"\"", "'\"")
t("1 /* a */ /* b */ + /* c */ 1", "2")
t("0.5.toFixed(1) + 5..toString()", "0.55")
t("1.5e3 + .5", "1500.5")
t("({}).a?.b ?? 'dflt'", "dflt")
t("[1, 2, 3].includes(2) ? 'yes' : 'no'", "yes")
t("var tpl = 'x'; `a${`b${tpl}c`}d`", "abxcd")
t("`${1}${2}` + `\\${no}` + `a\\`b`", "12${no}a`b")

---------------------------------------------------------------------------
-- benchmark (not counted as checks)
---------------------------------------------------------------------------
do
  local r = js.new({ budget = 1e9 })
  local c0 = os.clock()
  local ok, v = r:run("function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2) } fib(24)", "bench")
  local c1 = os.clock()
  print(string.format("bench: fib(24) = %s in %.3f s", tostring(v), c1 - c0))
  ok, v = r:run("var a = []; for (var i = 0; i < 100000; i++) { a.push(i * 2) } a.length", "bench2")
  local c2 = os.clock()
  print(string.format("bench: 100k loop with push = %s in %.3f s", tostring(v), c2 - c1))
  ok, v = r:run("var s = 0; var a2 = []; for (let i = 0; i < 50000; i++) a2[i] = i; for (const x of a2) s += x; a2.map(x => x + 1).filter(x => x % 2).length + s", "bench3")
  local c3 = os.clock()
  print(string.format("bench: map/filter/for-of over 50k = %s in %.3f s", tostring(v), c3 - c2))
end

print(string.format("%d checks, %d failed", checks, fails))
if fails > 0 then os.exit(1) end
