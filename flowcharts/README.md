# Flowcharts

Diagrams are written in [Mermaid](https://mermaid.js.org/) (`.mmd`) and rendered to SVG.

## Regenerating SVGs

Requires Node.js. No global install needed — `npx` downloads the CLI on first run.

Regenerate a single file:

```sh
npx @mermaid-js/mermaid-cli -i <file>.mmd -o <file>.svg
```

Regenerate all files at once (run from the `flowcharts/` directory):

```sh
for f in *.mmd; do npx @mermaid-js/mermaid-cli -i "$f" -o "${f%.mmd}.svg"; done
```

## Syntax note

Node labels that contain parentheses `()` must be wrapped in double quotes, otherwise the Mermaid parser will error:

```
# Wrong
A --> B[myFunc()\nsome text]

# Correct
A --> B["myFunc()\nsome text"]
```

This applies to all node shapes: `[]`, `{}`, `()`, etc.
