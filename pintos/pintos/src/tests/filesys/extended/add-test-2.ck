# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(add-test-2) begin
(add-test-2) create ("y..."): 1
(add-test-2) writing 64 KB...
(add-test-2) now reading 64 KB...
(add-test-2) Number of device writes is on the order of 128
(add-test-2) end
EOF
pass;