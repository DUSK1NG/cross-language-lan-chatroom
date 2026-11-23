#![cfg_attr(target_os = "windows", windows_subsystem = "windows")]

use std::{
    env,
    path::PathBuf,
    process::{Command, ExitCode},
};

use anyhow::{Context, Result, bail, ensure};

fn run() -> Result<()> {
    let mut root = None;
    let mut dry_run = false;
    let mut args = env::args_os().skip(1);
    while let Some(arg) = args.next() {
        match arg.to_str() {
            Some("--dry-run") => dry_run = true,
            Some("--root") => {
                root = Some(PathBuf::from(
                    args.next().context("--root requires a path")?,
                ))
            }
            _ => bail!("Unexpected launcher argument: {}", arg.to_string_lossy()),
        }
    }

    let script_path = "scripts/bootstrap-github.ps1";
    let root = match root {
        Some(path) => env::current_dir()?.join(path),
        None => env::current_exe()?
            .ancestors()
            .skip(1)
            .take(10)
            .find(|directory| directory.join(script_path).is_file())
            .context("Keep LANChat-Launcher.exe inside the LAN Chat source package")?
            .to_owned(),
    };
    let script = root.join(script_path);
    ensure!(script.is_file(), "Missing {}", script.display());
    if dry_run {
        println!("{}", script.display());
    } else {
        let mut command = Command::new("powershell.exe");
        command
            .args([
                "-NoLogo",
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
            ])
            .arg(&script)
            .current_dir(&root);
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x00000010); // CREATE_NEW_CONSOLE
        }
        command
            .spawn()
            .with_context(|| format!("Start {}", script.display()))?;
    }
    Ok(())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error:#}");
            #[cfg(windows)]
            if !env::args_os().any(|arg| arg == "--dry-run") {
                use windows_sys::Win32::UI::WindowsAndMessaging::{
                    MB_ICONERROR, MB_OK, MessageBoxW,
                };
                let message: Vec<_> = format!("{error:#}\0").encode_utf16().collect();
                let title: Vec<_> = "LAN Chat source launcher\0".encode_utf16().collect();
                unsafe {
                    MessageBoxW(
                        std::ptr::null_mut(),
                        message.as_ptr(),
                        title.as_ptr(),
                        MB_ICONERROR | MB_OK,
                    )
                };
            }
            ExitCode::FAILURE
        }
    }
}
