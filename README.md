# Pico

Implementation of the Kilo text editor written by Salvatore Sanfilippo (antirez), following a guide written by Paige Ruten(snaptoken), at https://viewsourcecode.org/snaptoken/kilo/index.html .

This is currently similar to what is found on the guide, but I hope to development the program further in the future.

## Extensions Implemented:
- vim-like row numbering
- Python highlighting
- Vim-like modes, with ability to write commands
- Tree-view to allow change of file which is being worked on

## (Currently) Supported commands
- w
- q (and q!)
- np (paginated tree-view, currently working on implementing a branched tree-view)

## Problems
- Branched view is currently accessible but functionality is limited
- Tree-view cannot extend to and past .../ , but there is a possible solution to the issue.
