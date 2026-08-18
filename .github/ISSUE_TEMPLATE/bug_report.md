---
name: Bug Report
about: Something in the Windows client does not work
title: '[BUG] '
labels: bug
assignees: ''
---

## What happened

<!-- What went wrong, in one or two sentences. -->

## Steps to reproduce

1.
2.
3.

## Expected behaviour

<!-- What you expected instead. -->

## Screenshots

<!-- Optional. A screenshot of the app in the broken state helps a lot for UI bugs. -->

## Your setup

- **Windows edition and build**: <!-- run `winver`, e.g. Windows 11 Pro 24H2, build 26100.1742 -->
- **Dish version**: <!-- Settings > About, e.g. 1.0.0 -->
- **How you installed it**: <!-- release zip, or built from source (give the commit) -->
- **Satellite version and platform**: <!-- e.g. satellite 0.4.1 on Ubuntu 24.04 / Windows 11 -->

## Your controller

Skip this whole section if the bug has nothing to do with a controller.

- **Make and model**: <!-- e.g. Sony DualSense CFI-ZCT1W, 8BitDo Ultimate 2C -->
- **How it is connected**: <!-- USB cable / Bluetooth / vendor dongle (2.4 GHz receiver) -->
- **Input path**: <!-- SDL (the default) or USB-direct, if you switched it in the app -->
- **Does the pad work elsewhere?** <!-- Press Win+R, run `joy.cpl`, open Properties, and
     move the sticks. Or test it in Steam or any game. Say which you tried and whether
     the pad responded. -->

### The `DEVCAPS` log line

Dish writes one `DEVCAPS` line per controller the moment it attaches. It names
the controller, the vendor and product id, the GUID, and which capabilities were
detected. It is the single most useful thing you can attach to a controller bug.

To capture it, open PowerShell in the folder that contains `dish.exe` and run:

```powershell
Start-Process .\dish.exe -RedirectStandardError dish-log.txt
```

Now plug in or connect the controller, reproduce the problem, close Dish, and
open `dish-log.txt`. Paste the line or lines starting with `DEVCAPS`. They look
roughly like this:

```
dish.input: DEVCAPS id= "..." name= "Wireless Controller" type= 4 vid= "54c" pid= "ce6" guid= "..." gyro= true accel= true led= true bt= false
```

If no `DEVCAPS` line appears at all, say so, and try again after setting
`$env:QT_LOGGING_RULES = "dish.*=true"` in the same PowerShell window before
launching. A missing line is itself a useful finding: it means the controller was
never seen.

<!-- Paste DEVCAPS here -->

```
```

## If the app crashed

A crash writes `crash.log` and `crash.dmp` to `%LOCALAPPDATA%\Dish\`. Paste the
contents of `crash.log`, which is plain text.

**Do not attach `crash.dmp` to a public issue** unless a maintainer asks. It is a
memory snapshot of the process and can contain the satellite address you were
connected to and, in principle, key material.

<!-- Paste crash.log here -->

```
```

## Anything else

<!-- Network setup, VPN, firewall rules, multiple satellites, anything unusual. -->
