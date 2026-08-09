# Reporting an issue

Issues, feature requests and documentation corrections all go to the main repository:

[videosynth GitHub repository](https://github.com/decode-orc/videosynth/issues){target="_blank"}

## What to include

videosynth is deterministic, which makes bug reports unusually easy to act on. If you can supply the project file, the problem can be reproduced exactly.

Please include:

1. **The project YAML** that reproduces the problem, ideally cut down to the smallest version that still shows it. If it references your own media, say what the source is and whether a bundled asset shows the same behaviour.
2. **The exact command** you ran, or the sequence of GUI actions.
3. **The version** — `videosynth --version`, or **Help → About** in the GUI. This is the git commit the binary was built from.
4. **The log**, ideally at `--log-level debug`:

   ```bash
   videosynth --project my.yaml --log-level debug --log-file issue.log
   ```

5. **What you expected** and **what happened instead**.

For a signal-correctness problem, the frame number and the line number involved are worth a great deal — as is the clause of the standard you believe is being violated, if you know it.

## Documentation issues

Corrections to this documentation are just as welcome as code issues. The site is built with MkDocs from the `docs/` directory of the repository, so a documentation fix is an ordinary pull request against the Markdown file.

If you are not sure where a correction belongs, open an issue and say what you were trying to find out and where you looked for it — that is useful information in its own right.

## Security issues

Report suspected security issues privately through GitHub Security Advisories rather than as a public issue.
