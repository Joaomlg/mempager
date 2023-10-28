# tests

This repository stores tests for the assignment.  Tests are numbered starting from 1.  For each test, we store the C source, the output printed by the test itself (`.out` files) and the output printed by the reference pager (`.mmu.out`). The reference `.mmu.out` files are used to grade the assignment.

Tests are linked against the UVM module in the [programming
assignment][1], and run with a dedicated instance of the MMU module.
When creating new tests, you need to specify the number of frames
and number of blocks that should be used in the MMU module.  The
number of frames and blocks go in the `tests.spec` file, where each
line has the following format:

```
test-id num-frames num-blocks
```

  [1]: https://gitlab.dcc.ufmg.br/cunha-dcc605/mempager-assignment

! vim: tw=68
