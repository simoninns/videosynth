# Demonstration projects

A small, curated set of runnable project files that together exercise every
feature of videosynth. Each file is self-documenting (see its header comment)
and generates quickly (8 frames per section).

Run one directly:

```
nix develop "path:$PWD" --command ./build/videosynth --project docs/examples/pal_cav_disc.yaml
```

or run them all with `./run-examples.sh`. Outputs are written to
`docs/examples/output/` (git-ignored). The metadata sidecar, the `.c` chroma
file (for Y/C output) and the audio `.wav` are always colocated with the video.
Projects that enable `output.efm_audio` also write a LaserDisc digital audio
`.efm` T-value stream for the selected channel pair beside its `.wav`.

Paths use logical asset roots — `{bundled}` is the installed asset library and
`{project}` is the example's own directory.

| File | Standard | Signal | Highlights |
|------|----------|--------|------------|
| `pal_cav_disc.yaml` | PAL | composite | CAV biphase, pilot burst, VITS, OSD, noise, dropouts, audio + EFM, EXR+MKV |
| `pal_clv_disc.yaml` | PAL | composite | CLV biphase codes, VITS, OSD, noise |
| `ntsc_cav_disc.yaml` | NTSC | composite | CAV biphase + FM codes, VIRS, OSD, noise, dropouts, audio + EFM |
| `ntsc_clv_disc.yaml` | NTSC | composite | CLV + FM codes, VIRS, OSD, noise |
| `pal_m_cav_disc.yaml` | PAL-M | composite | CAV biphase + FM codes, VIRS, OSD, noise |
| `pal_signal_features.yaml` | PAL | composite | full PAL VITS set, noise, random/scratch dropouts, audio, OSD |
| `ntsc_signal_features.yaml` | NTSC | composite | full NTSC VITS set, noise, dropouts, audio, OSD |
| `pal_yc_disc.yaml` | PAL | Y/C | dual-file luma/chroma output over a CAV disc |

For the full biphase reference see [../user/biphase-design.md](../user/biphase-design.md).
