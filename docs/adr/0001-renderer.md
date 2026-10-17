# ADR 0001 — Viewport renderer

## Status

Accepted for the Linux baseline.

## Context

The solid has to be an OCCT shape. The screen needs selection, orbit, and a light stylized look, including on a later Android tablet. Qt Quick 3D was rejected: on the Qt 6.8 license table it is GPL-only, and it is a scene-graph renderer rather than a CAD picker. OCCT's own AIS/OpenGL viewer pulls the visualization toolkit, fights Qt Quick's scene graph, and is a poor fit for Android.

## Decision

OCCT remains the solid. `BRepMesh_IncrementalMesh` produces the triangles. A `QQuickFramebufferObject` draws them with Lambert-style wrap lighting and an amber selection mix. Picking renders integer part ids into an `GL_R32I` framebuffer and reads them back with `GL_RED_INTEGER`. Id 0 means the click missed. `GL_R32I` and integer fragment outputs exist in OpenGL 3.3 and in OpenGL ES 3.0, so the picker does not depend on a desktop-only feature.

The shader bodies are shared. The version line is chosen from the current context: `#version 330 core` when the context is desktop GL, `#version 300 es` with `highp` precision when `QOpenGLContext::isOpenGLES()` is true. On Android the surface format is OpenGL ES 3.0 with no profile. On Linux it stays OpenGL 3.3 core. The process calls `QQuickWindow::setGraphicsApi(OpenGL)` and sets that format before the application object exists. The framebuffer item does not render if Qt has already chosen Vulkan, Metal, or D3D.

## Consequences

- Face and edge picking are not implemented. Selection is the whole part.
- Dimension annotations are not drawn in the viewport. Numbers live in the inspector.
- Desktop GL 3.3 is verified on this machine's Mesa driver. The ES 3.00 sources are in the same binary and are selected only for an ES context. No Android package has been compiled or run, so the tablet path is unproven.
- Mesh quality changes the triangle count, not the solid.
