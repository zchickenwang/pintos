# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(test1) begin
(test1) create("x..."): 1
(test1) writing 60 blocks...
(test1) now reading 60 blocks...
(test1) Miss rate ratio is smaller or same than cold cache
(test1) end
EOF
pass;
