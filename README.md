# TCP
Custom TCP implementation

## File Layout
- src/: Source files
  - 3a/: Part 3a source files
    - reliable.c: C source file for 3a implementation
    - rlib.c|h: C source and header files for 3a helper library
    - Makefile: Makefile to build 3a
    - reference: Reference implementation of 3a
    - stripsol: ???
    - tester: Tester binary
    - tester-src/: Tester source, in Haskell
  - 3b/: Part 3b source files
    - generator: Random file generator binary
    - relayer/: Relayer binary and config file
      - config.xml: Bundled relayer configuration (from zip of 3b)
      - config-standalone.xml: Configuration from project page
      - relayer: relayer binary
      - measure_bandwidth/: Relayer bandwidth measurement utility source files
        - readme: README
        - receiver.cpp: Receiver end of measurement utility
        - sender.cpp: Sender end of measurement utility
    - reliable/: Implementation of 3b
      - reliable.c: C source file for 3b implementation
      - rlib.c|h: C source and header files for 3b helper library
      - Makefile: Makefile to build 3b
      - stripsol: ???

NOTE: (rlib|reliable).(c|h) are not the same between 3a and 3b

