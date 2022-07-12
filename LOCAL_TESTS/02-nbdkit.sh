#!/bin/bash
../nbdkit -t 32 -v -f -e local_test -p 10000 chunked
