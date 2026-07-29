//! Workspace automation tasks. Run with `cargo xtask <task>`.
//!
//! Tasks:
//!   gen-proto   Regenerate the committed protobuf sources under
//!               `crates/pico-view/src/proto/` from `proto/*.proto`.
//!


use std::path::{Path, PathBuf};
use std::process::exit;

fn main() {
    match std::env::args().nth(1).as_deref() {
        Some("gen-proto") => gen_proto(),
        Some(other) => {
            eprintln!("unknown task: {other}\n");
            print_help();
            exit(1);
        }
        None => {
            print_help();
            exit(1);
        }
    }
}

fn print_help() {
    eprintln!(
        "usage: cargo xtask <task>\n\ntasks:\n  \
         gen-proto   regenerate committed protobuf sources in crates/pico-view/src/proto/"
    );
}

fn gen_proto() {
    let root = workspace_root();
    let proto_dir = root.join("proto");
    let out_dir = root.join("crates/pico-view/src/proto");

    let protos = [
        proto_dir.join("pv_ffi.proto"),
        proto_dir.join("pv_wire.proto"),
    ];
    let includes = [proto_dir.as_path()];

    std::fs::create_dir_all(&out_dir)
        .unwrap_or_else(|e| panic!("could not create {}: {e}", out_dir.display()));

    println!("regenerating protobuf sources -> {}", out_dir.display());
    prost_build::Config::new()
        .out_dir(&out_dir)
        .compile_protos(&protos, &includes)
        .unwrap_or_else(|e| {
            eprintln!("protoc failed compiling {}: {e}\nis protoc installed and on PATH?", proto_dir.display());
            exit(1);
        });
    println!("done — review and commit the changes under {}", out_dir.display());
}

/// `<root>/crates/xtask` -> `<root>`.
fn workspace_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .expect("xtask manifest should live at <root>/crates/xtask")
        .to_path_buf()
}
