#!/bin/bash

python3 -m grpc.tools.protoc --nanopb_out=. -I. samples.proto
