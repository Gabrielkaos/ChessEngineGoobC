# GOOB CHESS ENGINE - OFFICIAL REPO
UCI playing Chess Engine written in C

![](https://github.com/Gabrielkaos/ChessEngineGoobC/blob/main/logo/GOOBLOGO2.bmp)

## Update - Oct 3 2026 - Gabriel Montes
Finally NNUE is implemented and trained using open source lichess data positions. NNUE architecture is from Schoenemann(see below). The architecture was very easy to train even with only 300M
positions, unlike stockfish architecture probably needing at least 1 billion rows of data. I am planning on releasing new version in 2027, stay tuned. Also, finally got a job :).

## How to Use
Compile the code in the src directory using the makefile.

## About
Check src/others/goob_commits.txt(outdated, file was made before discovering git)
Check README on /src and /tools

## Future plans
* Buy hardware for future constant tuning, nnue training.
* Fetch more nnue training data.

## Humble beginnings
After watching Bluefever's tutorial on how to make a chess engine in C. I got curious to how other engines manage to get very strong and fast. I asked on reddit, stackoverflow about how to implement things that can make a chess engine fast. I got interested in the idea of bitboards, representing 64 squares using the 64 bit long integer data type, That's when I discovered BBC a chess engine that uses this kind of board representation.
I watched CodeMonkeyKings's tutorial on BBC, after implementing the bitboards, I searched on chessprogrammingwiki about techniques and other things. After 2 months of tinkering, I was finally satisfied.
Just trying to see how far I can make GOOB stronger by watching, reading, and researching how other open source engines work :)


## NNUE Evaluation
GOOB uses a modern NNUE (Efficiently Updatable Neural Network) evaluation function based on the **Schoenemann-0.5.0** architecture:
* **Architecture:** Perspective-aware dual-accumulator topology `(768 -> 1024)x2 -> 1x8` with Squared Clipped ReLU (SCReLU) activation and 8 material buckets.
* **Inference Code:** SIMD-accelerated inference adapted from [Schoenemann](https://github.com/Jochengehtab/Schoenemann) by Jochen Gehtab (licensed under GNU AGPL-3.0).
* **Data & Training:** Network weights (`quantised.bin`) and datasets were independently produced and trained by Gabriel Montes using custom data and training pipelines (`tools/nnue_project/`).

## Credits
##### Credits to everyone who inspired and helped me

###### Some very helpful people
* [Chessprogramming - maker of BBC](https://www.youtube.com/@chessprogramming591)
* [Bluefever Sofware's channel - maker of Vice](https://www.youtube.com/user/BlueFeverSoft)
* [Chess Programming's channel](https://www.youtube.com/channel/UCB9-prLkPwgvlKKqDgXhsMQ)
* [Chessprogrammingwiki](https://www.chessprogramming.org/Main_Page)
* [Chess Coding Adventure](https://youtu.be/U4ogK0MIzqk)

###### Some very inspiring engines I used for reference
* [Schoenemann](https://github.com/Jochengehtab/Schoenemann) by Jochen Gehtab – NNUE network architecture and SIMD inference reference (AGPL-3.0)
* [Ethereal Chess Engine by Andrew Grant](https://github.com/AndyGrant/Ethereal)
* Vice
* [BBC](https://github.com/maksimKorzh/bbc)
* Engine made by Sebastian Lague in one of his video
* [Stockfish](https://github.com/official-stockfish/stockfish)
* [Berserk](https://github.com/jhonnold/berserk)
