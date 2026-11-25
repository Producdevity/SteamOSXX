# Game compatibility

Device: RG40XX-H (H700, 4x Cortex-A53 ARMv8.0, Mali-G31, 1 GB RAM), 640x480. Steam arm64 client in
Big Picture; Windows games through Proton ARM64 + FEX. D3D8/9 default to WineD3D on the GPU, D3D10/11 to DXVK on llvmpipe.

| Game (appid) | Compat tool | Renderer | Result | FPS | Notes |
|---|---|---|---|---|---|
| Geometry Wars: Retro Evolved (8400) | Proton Experimental (ARM64) | DXVK / llvmpipe | Runs, playable input | 6.4 intro, 1.3-3.0 menu | menu 3.0 with the web helper stopped |
| Geometry Wars: Retro Evolved (8400) | Proton Experimental (ARM64) | WineD3D / Panfrost GL 3.1 | Runs | 4.5 intro | the colour problem was a screenshot artifact |
| Geometry Wars: Retro Evolved (8400) | proton-armv8 11.0-3 (sunshineinabox) | WineD3D / Panfrost (default) | Runs, correct, full Steam menu round trip | 8.6 title | ~1.5-2.5 min to the title screen |
| Geometry Wars: Retro Evolved (8400) | proton-armv8 11.0-3 (sunshineinabox) | DXVK / llvmpipe | Runs; comes to the front on launch, Steam button menu and back work | 1.4-3.8 menu | ~2 min to the title screen; 441 MB |

How to test a title: install it from Big Picture, then
`sxx eval 'SteamClient.Apps.SetAppLaunchOptions(<appid>, "DXVK_HUD=fps /usr/local/bin/sxx-wrap %command%")'`
and `sxx eval 'SteamClient.Apps.RunGame("<appid>","",-1,100)'`; the wrapper log is `/tmp/sxx-wrap-<appid>.log`.
