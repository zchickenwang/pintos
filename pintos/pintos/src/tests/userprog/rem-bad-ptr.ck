# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF', <<'EOF']);
(rem-bad-ptr) begin
(rem-bad-ptr) end
rem-bad-ptr: exit(0)
EOF
(rem-bad-ptr) begin
rem-bad-ptr: exit(-1)
EOF
pass;
