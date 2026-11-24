# Game compatibility

Device: RG40XX-H (H700, 4x Cortex-A53 ARMv8.0, Mali-G31, 1 GB RAM), 640x480. Steam arm64 client in
Big Picture; Windows games through Proton ARM64 + FEX; DXVK on llvmpipe unless noted.

| Game (appid) | Compat tool | Renderer | Result | FPS | Notes |
|---|---|---|---|---|---|
| Geometry Wars: Retro Evolved (8400) | Proton Experimental (ARM64) | DXVK / llvmpipe | Runs, playable input | 6.4 intro, 1.3-3.0 menu | menu 3.0 with the web helper stopped |
| Geometry Wars: Retro Evolved (8400) | Proton Experimental (ARM64) | WineD3D / Panfrost GL 3.1 | Runs, wrong colours | 4.5 intro | `PROTON_USE_WINED3D=1` |
| Geometry Wars: Retro Evolved (8400) | proton-armv8 11.0-3 (sunshineinabox) | DXVK / llvmpipe | Starts; hidden behind Big Picture, then memory livelock | - | before `sxx-focus`, earlyoom, swapfile |

How to test a title: install it from Big Picture, then
`sxx eval 'SteamClient.Apps.SetAppLaunchOptions(<appid>, "DXVK_HUD=fps /usr/local/bin/sxx-wrap %command%")'`
and `sxx eval 'SteamClient.Apps.RunGame("<appid>","",-1,100)'`; the wrapper log is `/tmp/sxx-wrap-<appid>.log`.
