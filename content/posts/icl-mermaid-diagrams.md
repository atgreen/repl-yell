---
title: "Visualizing Diagrams in the REPL with ICL and Mermaid"
date: 2024-12-24
draft: false
tags: ["lisp", "icl", "visualization", "mermaid"]
summary: "ICL 1.14.1 adds Mermaid diagram support - render flowcharts, sequence diagrams, state machines, and more directly from your REPL."
---

One of the things I love about Lisp development is the tight feedback loop. You write code, evaluate it, see the result. But sometimes the result is better understood as a picture than as text.

[ICL](https://github.com/atgreen/icl) (Interactive Common Lisp) is an enhanced REPL I've been building that includes a browser-based interface with visualization capabilities. Version 1.14.1 adds support for [Mermaid](https://mermaid.js.org/) diagrams.

## What's Mermaid?

Mermaid is a JavaScript library that renders diagrams from text definitions. Instead of wrestling with a drawing tool, you describe your diagram in a simple DSL:

```
graph TD
    A[Start] --> B{Decision}
    B -->|Yes| C[Do something]
    B -->|No| D[Do something else]
```

ICL now detects Mermaid definitions and renders them automatically.

## Using Mermaid in ICL

Start ICL in browser mode:

```bash
icl -b
```

Then use the `,viz` command with a Mermaid string:

```lisp
ICL> ,viz "graph TD
    A[REPL] --> B[Evaluate]
    B --> C[Print]
    C --> A"
```

ICL auto-detects the diagram type and renders it in a panel. Supported diagram types include:

- `graph` / `flowchart` - Flow diagrams
- `sequenceDiagram` - Sequence diagrams
- `classDiagram` - Class diagrams
- `stateDiagram` - State machines
- `erDiagram` - Entity-relationship diagrams
- `gantt` - Gantt charts
- `pie` - Pie charts
- `gitgraph` - Git branch visualizations

## Custom Object Visualization

The real power comes from defining visualizations for your own objects. ICL provides a generic function `icl-runtime:visualize` that you can specialize:

```lisp
(defclass state-machine ()
  ((name :initarg :name :accessor sm-name)
   (states :initarg :states :accessor sm-states)
   (transitions :initarg :transitions :accessor sm-transitions)
   (initial :initarg :initial :accessor sm-initial)))

(defun state-machine-to-mermaid (sm)
  "Convert state-machine to a Mermaid state diagram."
  (with-output-to-string (s)
    (format s "stateDiagram-v2~%")
    (format s "    [*] --> ~A~%" (sm-initial sm))
    (loop for (from to label) in (sm-transitions sm)
          do (format s "    ~A --> ~A: ~A~%" from to label))))

(defmethod icl-runtime:visualize ((obj state-machine))
  (list :mermaid (state-machine-to-mermaid obj)))
```

Now when you evaluate a state machine object, ICL can render it as a diagram:

```lisp
ICL> (defvar *http-states*
       (make-instance 'state-machine
         :name "HTTP Connection"
         :initial 'idle
         :transitions '((idle connecting "connect()")
                        (connecting connected "success")
                        (connecting error "timeout")
                        (connected idle "disconnect()")
                        (error closed "cleanup"))))

ICL> ,viz *http-states*
; Visualizing Mermaid diagram
```

## Theme Support

Diagrams automatically adapt to ICL's current theme. Switch between light and dark mode and the diagrams re-render with appropriate colors.

## Get ICL

ICL is available on [GitHub](https://github.com/atgreen/icl). Binaries are provided for Linux, macOS, and Windows.

```bash
# macOS
brew install atgreen/icl/icl

# Or download from releases
```

The Mermaid support is in version 1.14.1 and later. See `examples/mermaid.lisp` in the repository for more examples.
