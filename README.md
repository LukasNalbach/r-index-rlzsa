r-index: the run-length BWT index
===============
Author: Nicola Prezza (nicola.prezza@gmail.com)
Joint work with Travis Gagie and Gonzalo Navarro

rlzsa has been added according to [1] and [2]


cite as:

Gagie T, Navarro G, Prezza N. Optimal-time text indexing in BWT-runs bounded space. In Proceedings of the Twenty-Ninth Annual ACM-SIAM Symposium on Discrete Algorithms, SODA 2018, New Orleans, NA, USA, January 7-10 2017.

Travis Gagie, Gonzalo Navarro, and Nicola Prezza. Fully Functional Suffix Trees and Optimal Text Searching in BWT-Runs Bounded Space. J. ACM 67, 1, Article 2 (April 2020)

### Brief description

The r-index is the first full-text index of size O(r+z'), r being the number of BWT runs of the input text (of size n) and z' is the number of phrases in the relative lempel-ziv encoded differential suffix array (see [1]).

Let s be the alphabet size and fix a constant eps>0. The r-index offers the following tradeoffs:

- Space: r * ( log s + (1+eps)log(n/r) + 2log n ) + O(r log n) + O(z' log n) bits
- Count time: O( (m/eps) * (log (n/r) + log s) )
- Locate time: After count, O( 1 ) time per occurrence, but O( r ) time overall (worst-case)

On very repetitive datasets, the r-index locates orders of magnitude faster than the RLCSA (with a sampling rate resulting in the same size for the two indexes).

### Download

To clone the repository, run:

> git clone https://github.com/LukasNalbach/r-index-rlzsa

### Compile

We use cmake to generate the Makefile. Create a build folder in the main r-index folder:

> mkdir build

run cmake:

> cd build; cmake ..

and compile:

> make

### Run

After compiling, run 

>  ri-build input

This command will create the r-index of the text file "input" and will store it as "input.ri". Use option -o to specify a different basename for the index file. 

Run

> ri-count index.ri patterns

to count number of occurrences of the patterns, where <patterns> is a file containing the patterns in pizza&chili format (http://pizzachili.dcc.uchile.cl/experiments.html). To generate pattern files, use the tool http://pizzachili.dcc.uchile.cl/utils/genpatterns.c. Note: the Pizza&chili format consists of a header followed by newline followed by all the patterns concatenated (without any separator). The patterns must all be of the same length.

Run

> ri-locate index.ri patterns

to locate all occurrences of the patterns.

Be aware that the above executables are just benchmarking tools: no output is generated (pattern occurrences are deleted after being extracted and not printed to output).

### Funding

Nicola Prezza has been supported by the project Italian MIUR-SIR CMACBioSeq ("Combinatorial methods for analysis and compression of biological sequences") grant n.~RBSI146R5L, PI: Giovanna Rosone. Link: http://pages.di.unipi.it/rosone/CMACBioSeq.html

### References
[1] Simon J. Puglisi and Bella Zhukova. Smaller RLZ-Compressed Suffix Arrays. In 31st Data Compression Conference (DCC), 2021 ([pdf](https://ieeexplore.ieee.org/document/9418726))

[2] Bella Zhukova, New space-time trade-offs for pattern matching with compressed indexes, PhD Thesis 2024 ([pdf](https://helda.helsinki.fi/items/a672a30c-0611-408c-abeb-36aa069df2a1))