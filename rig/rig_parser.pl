% rig_parser.pl
% A simplified DCG parser for the TinyRig DSL with object-prefixed functions.
% Compatible with Trealla Prolog (zero-dependency, unmoduled).

:- set_prolog_flag(double_quotes, chars).

%% ---------------------------------------------------------
%% Public API
%% ---------------------------------------------------------

% verify_rigging_program(+String)
% True if String contains a valid, simplified TinyRig program.
verify_rigging_program(String) :-
    phrase(program, String).

write_rigging_program(Seconds) :-
    Parts = [
        'Hips', 'Spine', 'Chest', 'Neck', 'Head',
        'LeftShoulder', 'LeftUpperArm', 'LeftLowerArm', 'LeftHand',
        'RightShoulder', 'RightUpperArm', 'RightLowerArm', 'RightHand',
        'LeftUpperLeg', 'LeftLowerLeg', 'LeftFoot',
        'RightUpperLeg', 'RightLowerLeg', 'RightFoot'
    ],
    setup_call_cleanup(
        open('script.rig', write, Stream),
        generate_rigging_lines(Stream, Parts, Seconds),
        close(Stream)
    ).

% write_rigging_program(+StrideFrequency, +StepAmplitude)
% Generates an arbitrarily parameterized, valid walk cycle script and writes it to script.rig.
write_rigging_program(StrideFrequency, StepAmplitude) :-
    Parts = [
        'Hips', 'Spine', 'Chest', 'Neck', 'Head',
        'LeftShoulder', 'LeftUpperArm', 'LeftLowerArm', 'LeftHand',
        'RightShoulder', 'RightUpperArm', 'RightLowerArm', 'RightHand',
        'LeftUpperLeg', 'LeftLowerLeg', 'LeftFoot',
        'RightUpperLeg', 'RightLowerLeg', 'RightFoot'
    ],
    setup_call_cleanup(
        open('script.rig', write, Stream),
        generate_walk_cycle_lines(Stream, Parts, StrideFrequency, StepAmplitude),
        close(Stream)
    ).

generate_rigging_lines(_, [], _).
generate_rigging_lines(Stream, [Part | Rest], Seconds) :-
    generate_commands_for_part(Stream, Part, Seconds),
    generate_rigging_lines(Stream, Rest, Seconds).

generate_commands_for_part(Stream, Part, Seconds) :-
    format(atom(TransLine), '~w: ~w_translate(0.0, ~w, 0.0);\n', [Part, Part, Seconds]),
    write(Stream, TransLine),
    format(atom(OscLine), '~w: ~w_oscillate(x, 0.5, ~w);\n', [Part, Part, Seconds]),
    write(Stream, OscLine),
    format(atom(OrbitLine), '~w: ~w_orbit(1.2, 0.2, y);\n', [Part, Part]),
    write(Stream, OrbitLine).

generate_walk_cycle_lines(_, [], _, _).
generate_walk_cycle_lines(Stream, [Part | Rest], Freq, Amp) :-
    generate_walk_commands_for_part(Stream, Part, Freq, Amp),
    generate_walk_cycle_lines(Stream, Rest, Freq, Amp).

generate_walk_commands_for_part(Stream, 'Hips', Freq, Amp) :-
    !,
    format(Stream, 'Hips: Hips_translate(0.0, 0.0, 0.0);\n', []),
    format(Stream, 'Hips: Hips_oscillate(y, ~w, ~w);\n', [Freq, Amp]),
    format(Stream, 'Hips: Hips_oscillate(z, ~w, ~w);\n', [Freq, Amp]).
generate_walk_commands_for_part(Stream, 'LeftUpperLeg', Freq, Amp) :-
    !,
    format(Stream, 'LeftUpperLeg: LeftUpperLeg_translate(0.0, 0.0, 0.0);\n', []),
    format(Stream, 'LeftUpperLeg: LeftUpperLeg_oscillate(x, ~w, ~w);\n', [Freq, Amp]).
generate_walk_commands_for_part(Stream, 'RightUpperLeg', Freq, Amp) :-
    !,
    format(Stream, 'RightUpperLeg: RightUpperLeg_translate(0.0, 0.0, 0.0);\n', []),
    format(Stream, 'RightUpperLeg: RightUpperLeg_oscillate(x, ~w, -~w);\n', [Freq, Amp]).
generate_walk_commands_for_part(Stream, 'LeftLowerLeg', Freq, Amp) :-
    !,
    format(Stream, 'LeftLowerLeg: LeftLowerLeg_translate(0.0, 0.0, 0.0);\n', []),
    format(Stream, 'LeftLowerLeg: LeftLowerLeg_oscillate(x, ~w, ~w);\n', [Freq, Amp]).
generate_walk_commands_for_part(Stream, 'RightLowerLeg', Freq, Amp) :-
    !,
    format(Stream, 'RightLowerLeg: RightLowerLeg_translate(0.0, 0.0, 0.0);\n', []),
    format(Stream, 'RightLowerLeg: RightLowerLeg_oscillate(x, ~w, -~w);\n', [Freq, Amp]).
generate_walk_commands_for_part(Stream, 'LeftUpperArm', Freq, Amp) :-
    !,
    format(Stream, 'LeftUpperArm: LeftUpperArm_translate(0.0, 0.0, 0.0);\n', []),
    format(Stream, 'LeftUpperArm: LeftUpperArm_oscillate(x, ~w, -~w);\n', [Freq, Amp]).
generate_walk_commands_for_part(Stream, 'RightUpperArm', Freq, Amp) :-
    !,
    format(Stream, 'RightUpperArm: RightUpperArm_translate(0.0, 0.0, 0.0);\n', []),
    format(Stream, 'RightUpperArm: RightUpperArm_oscillate(x, ~w, ~w);\n', [Freq, Amp]).
generate_walk_commands_for_part(Stream, Part, Freq, _) :-
    format(Stream, '~w: ~w_translate(0.0, 0.0, 0.0);\n', [Part, Part]),
    format(Stream, '~w: ~w_oscillate(x, ~w, 0.1);\n', [Part, Part, Freq]).

%% ---------------------------------------------------------
%% Stream-to-Char Reader for Trealla Compatibility
%% ---------------------------------------------------------

read_stream_to_chars(Stream, Chars) :-
    get_char(Stream, C0),
    read_stream_to_chars_loop(C0, Stream, Chars).

read_stream_to_chars_loop(end_of_file, _, []) :- !.
read_stream_to_chars_loop(C, Stream, [C | Rest]) :-
    get_char(Stream, NextC),
    read_stream_to_chars_loop(NextC, Stream, Rest).

%% ---------------------------------------------------------
%% DCG Grammar Rules
%% ---------------------------------------------------------

program --> ws, lines, ws.
program --> ws.

lines --> line, ws, lines.
lines --> line.

line --> 
    identifier(ObjName), ws, ":", ws1, command(ObjName).

command(Obj) --> 
    match_atom(Obj), "_translate(", ws, number, ws, ",", ws, number, ws, ",", ws, number, ws, ")", opt_semi.
command(Obj) --> 
    match_atom(Obj), "_rotate(", ws, axis, ws, ",", ws, number, ws, ",", ws, number, ws, ")", opt_semi.
command(Obj) --> 
    match_atom(Obj), "_scale(", ws, number, ws, ",", ws, number, ws, ",", ws, number, ws, ")", opt_semi.
command(Obj) --> 
    match_atom(Obj), "_uniform_scale(", ws, number, ws, ")", opt_semi.
command(Obj) --> 
    match_atom(Obj), "_oscillate(", ws, axis, ws, ",", ws, number, ws, ",", ws, number, ws, ")", opt_semi.
command(Obj) --> 
    match_atom(Obj), "_orbit(", ws, number, ws, ",", ws, number, ws, ",", ws, axis, ws, ")", opt_semi.

match_atom(Atom) --> 
    { atom_chars(Atom, Chars) }, 
    match_chars(Chars).

match_chars([]) --> [].
match_chars([C|Cs]) --> [C], match_chars(Cs).

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
    SimpleProgram = "Chest: Chest_translate(0.0, 1.13, 0.0);\nChest: Chest_uniform_scale(1.05);\nLeftUpperArm: LeftUpperArm_oscillate(x, 0.5, 2.0);\nHead: Head_orbit(1.2, 0.2, y);\n",
    (   verify_rigging_program(SimpleProgram)
    ->  write('Object-prefixed TinyRig program parsed successfully.'), nl
    ;   write('FAILED to parse program.'), nl
    ).

validate_script :-
    setup_call_cleanup(
        open('script.rig', read, Stream),
        read_stream_to_chars(Stream, Chars),
        close(Stream)
    ),
    (   verify_rigging_program(Chars)
    ->  write('VALID'), nl
    ;   write('INVALID'), nl
    ).