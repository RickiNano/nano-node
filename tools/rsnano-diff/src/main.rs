use crate::app::{App, Args};
use clap::Parser;

pub(crate) mod app;

fn main() -> anyhow::Result<()> {
    let args = Args::parse();
    App::default().run(args)
}
