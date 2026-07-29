//! Provisioning check: open the first pico-view device and
//! report its attestation verdict against the production CA.
//!
//!     cargo run --example attest_check
//!
//! This is a bench tool for the provisioning flow, not a gate: nothing in the
//! engine's normal open path refuses a device over attestation.

fn main() {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info")).init();

    let cfg = pico_view::PicoViewConfig::default();
    let panel = pico_view::resolve_panel(&cfg.model).expect("builtin panel preset");

    match pico_view::open_for_check(&cfg, &panel) {
        Ok(device_id) => println!("AUTH OK: genuine provisioned device '{device_id}'"),
        Err(e) => {
            println!("AUTH FAILED: {e}");
            std::process::exit(1);
        }
    }
}
