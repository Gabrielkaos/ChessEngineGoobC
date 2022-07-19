# ChessEngineGoobC
An UCI playing Chess Engine written in C

## Update - July 18, 2022
Just uploaded older_versions directory, all the beta versions of the latest build of the code are here, they are all experimental.
I've been studying Ethereal's Multithreading code for a while now. If I can finally(hopefully) understand the code, I will implement it and upload it here.

## GOOBGUI
Just something I made in python for fun, based on Vice code

## How to Use
Just compile the code in the src directory using the makefile. It should work without any problem.
I've tested it in windows and it works, don't know in Linux.

## About
#### Current Version 1.0.0
* A uci chess engine written in C. Uses the traditional MinMax algorithm in Negamax fashion. Uses few pruning techniques like AlphaBeta, LMR, futility pruning. 
* Has a simple evaluation code.
* It is currently stable in Windows 64 bit machines, don't know in Linux or Mac.

## Future plans
* Addind a **NNUE**(I'm still trying to understand how NNUE works, I might be able to understand it in maybe 6 years.)
* Maybe rewriting the whole thing in a much faster and safer language might be a good idea.
* Add more pruning techniques.

## Humble beginnings
After watching Bluefever's tutorial on how to make a chess engine in C. I got curious to how other engines manage to get very strong and fast. I asked on reddit, stackoverflow about how to implement things that can make a chess engine fast. I got interested in the idea of bitboards, representing 64 squares using the 64 bit long integer data type, That's when I discovered BBC a chess engine that uses this kind of board representation.
I watched CodeMonkeyKings's tutorial on BBC, after implementing the bitboards, I searched on chessprogrammingwiki about techniques and other things. After 2 months of tinkering, I was finally satisfied.


## Links
***Bluefever Software Youtube***
##### [Bluefever's channel](https://www.youtube.com/user/BlueFeverSoft)
***Chess Programming Youtube***
##### [Chess Programming's channel](https://www.youtube.com/channel/UCB9-prLkPwgvlKKqDgXhsMQ)
***Chess wiki***
##### [Chessprogrammingwiki](https://www.chessprogramming.org/Main_Page)
***Sebastian Lague's Chess Coding Adventure(this vid helped me too)***
##### [Chess Coding Adventure](https://youtu.be/U4ogK0MIzqk)
