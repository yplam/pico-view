# nanopb 0.4.9.1 runtime, vendored verbatim from https://github.com/nanopb/nanopb
# (zlib license, see LICENSE.txt). Only the runtime lives here; the generated
# pv_wire.pb.{c,h} are in main/gen and are regenerated with ../gen_proto.sh.
idf_component_register(
    SRCS "pb_common.c" "pb_encode.c" "pb_decode.c"
    INCLUDE_DIRS "."
)
