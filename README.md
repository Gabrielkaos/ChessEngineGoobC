# ChessEngineGoobC
An UCI playing Chess Engine written in C

## Update - July 28 2023
Been busy during highschool graduation, and now probably going to be more busy in college. Will soon be releasing version 2

## GOOBGUI
Just something I made in python for fun, based on Vice code

## How to Use
Compile the code in the src directory using the makefile.

## About
Check all in the comments in main.c from line 168 to 1558

## Future plans
* Addind a **NNUE**(I'm still trying to understand how NNUE works, I might be able to understand it in maybe 6 years.)
* Maybe rewriting the whole thing in a much faster and safer language might be a good idea.
* Add more pruning techniques.

## Humble beginnings
After watching Bluefever's tutorial on how to make a chess engine in C. I got curious to how other engines manage to get very strong and fast. I asked on reddit, stackoverflow about how to implement things that can make a chess engine fast. I got interested in the idea of bitboards, representing 64 squares using the 64 bit long integer data type, That's when I discovered BBC a chess engine that uses this kind of board representation.
I watched CodeMonkeyKings's tutorial on BBC, after implementing the bitboards, I searched on chessprogrammingwiki about techniques and other things. After 2 months of tinkering, I was finally satisfied.


## Credits
##### Credits to everyone who inspired and helped me

###### Some very helpful people
* [Chessprogramming - maker of BBC](https://www.youtube.com/@chessprogramming591)
* [Bluefever Sofware's channel - maker of Vice](https://www.youtube.com/user/BlueFeverSoft)
* [Chess Programming's channel](https://www.youtube.com/channel/UCB9-prLkPwgvlKKqDgXhsMQ)
* [Chessprogrammingwiki](https://www.chessprogramming.org/Main_Page)
* [Chess Coding Adventure](https://youtu.be/U4ogK0MIzqk)

###### Some very inspiring engines I used for reference
* [Ethereal Chess Engine by Andrew Grant](https://github.com/AndyGrant/Ethereal)
* Vice
* [BBC](https://github.com/maksimKorzh/bbc)
* Engine made by Sebastian Lague
