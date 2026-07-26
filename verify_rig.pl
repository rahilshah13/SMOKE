% rig_parser.pl
% A simplified DCG parser for the TinyRig DSL with object-prefixed functions.
% Compatible with Trealla Prolog (zero-dependency).

:- set_prolog_flag(double_quotes, chars).

%% ---------------------------------------------------------
%% Public API
%% ---------------------------------------------------------

% verify_rigging_program(+String)
% True if String contains a valid, simplified TinyRig program.
verify_rigging_program(String) :-
    phrase(program, String).

program --> ws, lines, ws.
program --> ws.

lines --> line, ws, lines.
lines --> line.

line --> 
    identifier(ObjName), ws, ":", ws1, command(ObjName).

command(Obj) --> 
    Obj, "_translate(", ws, number, ws, ",", ws, number, ws, ",", ws, number, ws, ")", opt_semi.
command(Obj) --> 
    Obj, "_rotate(", ws, axis, ws, ",", ws, number, ws, ")", opt_semi.
command(Obj) --> 
    Obj, "_scale(", ws, number, ws, ",", ws, number, ws, ",", ws, number, ws, ")", opt_semi.
command(Obj) --> 
    Obj, "_uniform_scale(", ws, number, ws, ")", opt_semi.
command(Obj) --> 
    Obj, "_oscillate(", ws, axis, ws, ",", ws, number, ws, ",", ws, number, ws, ")", opt_semi.
command(Obj) --> 
    Obj, "_orbit(", ws, number, ws, ",", ws, number, ws, ",", ws, axis, ws, ")", opt_semi.

axis --> "x" ; "y" ; "z" ; "X" ; "Y" ; "Z".

opt_semi --> ws, ";".
opt_semi --> [].

ws --> ws_char, ws.
ws --> [].

ws1 --> ws_char, ws.

ws_char --> [C], { is_space(C) }.

is_space(' ').
is_space('\t').
is_space('\n').
is_space('\r').

identifier(Atom) --> 
    [C], { is_alpha(C) }, identifier_rest(Chars),
    { atom_chars(Atom, [C | Chars]) }.

identifier_rest([C | Rest]) --> 
    [C], { is_alnum(C) ; C = '_' }, identifier_rest(Rest).
identifier_rest([]) --> 
    [].

number --> opt_sign, digits, opt_fraction.

opt_sign --> "-" ; "+" ; [].

digits --> [C], { is_digit(C) }, digits_rest.
digits_rest --> [C], { is_digit(C) }, digits_rest.
digits_rest --> [].

opt_fraction --> ".", digits.
opt_fraction --> [].

is_alpha(C) :- C @>= 'a', C @=< 'z'.
is_alpha(C) :- C @>= 'A', C @=< 'Z'.

is_digit(C) :- C @>= '0', C @=< '9'.

is_alnum(C) :- is_alpha(C).
is_alnum(C) :- is_digit(C).

%% ---------------------------------------------------------
%% Test Execution
%% ---------------------------------------------------------

test_simple :-
    SimpleProgram = "
        Chest: Chest_translate(0.0, 1.13, 0.0)
        Chest: Chest_uniform_scale(1.05)
        LeftUpperArm: LeftUpperArm_oscillate(x, 0.5, 2.0)
        Head: Head_orbit(1.2, 0.2, y)
    ",
    (   verify_rigging_program(SimpleProgram)
    ->  write('Object-prefixed TinyRig program parsed successfully.'), nl
    ;   write('FAILED to parse program.'), nl
    ).
