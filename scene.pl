:- use_module(library(random)).

set_seed(Seed) :- setrand(Seed).

rule(I, object(point(X, Y, Z), color(R, G, B), size(S))) :-
    I =< 4000,
    T is I * 0.05,
    random(RF),
    Radius is 2.5 + (RF * 0.5) - (I * 0.0005),
    X is cos(T) * Radius,
    Y is (I * 0.003) - 5.0,
    Z is sin(T) * Radius,
    R is 20 + floor(sin(T) * 100 + 100),
    G is 150 + floor(cos(T * 0.5) * 50),
    B is 255,
    S is 1.5 + (RF * 3.0).

rule(I, object(point(X, Y, Z), color(R, G, B), size(S))) :-
    I > 4000,
    random(RF1), random(RF2), random(RF3),
    X is (RF1 * 16.0) - 8.0,
    Y is (RF2 * 16.0) - 8.0,
    Z is (RF3 * 16.0) - 8.0,
    R is floor(RF1 * 30),
    G is floor(RF2 * 100),
    B is 80 + floor(RF3 * 100),
    S is 0.5 + (RF1 * 1.5).

emit_scenes(N, Seed) :-
    set_seed(Seed),
    random_between(10000, 300000, Count),
    findall([X, Y, Z, R, G, B, S], (between(1, Count, I), rule(I, object(point(X, Y, Z), color(R, G, B), size(S)))), List),
    format(string(FileName), "scene_~w.txt", [N]),
    open(FileName, write, Stream),
    write_points(Stream, List),
    close(Stream).

write_points(_, []).
write_points(S, [[X, Y, Z, R, G, B, Sz]|T]) :-
    format(S, "~f ~f ~f ~d ~d ~d ~f ", [X, Y, Z, R, G, B, Sz]),
    write_points(S, T).
