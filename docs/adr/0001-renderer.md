# ADR 0001 — Viewport renderer

## Status

Accepted for the Linux baseline.

## Context

The solid has to be an OCCT shape. The screen needs selection, orbit, and a light stylized look, including on a later Android tablet. Qt Quick 3D was rejected: on the Qt 6.8 license table it is GPL-only, and it is a scene-graph renderer rather than a CAD picker. OCCT's own AIS/OpenGL viewer pulls the visualization toolkit, fights Qt Quick's scene graph, and is a poor fit for Android.

## Decision

OCCT remains the solid. `BRepMesh_IncrementalMesh` produces the triangles. A `QQuickFramebufferObject` draws them with OpenGL 3.3 core, Lambert-style wrap lighting, and an amber selection mix. Picking renders integer part ids into an `GL_R32I` framebuffer and reads them back with `GL_RED_INTEGER`. Id 0 means the click missed.

The process calls `QQuickWindow::setGraphicsApi(OpenGL)` and sets a 3.3 core surface format before the application object exists. The framebuffer item does not render if Qt has already chosen Vulkan, Metal, or D3D.

## Consequences

- Face and edge picking are not implemented. Selection is the whole part.
- Dimension annotations are not drawn in the viewport. Numbers live in the inspector.
- Desktop GL 3.3 is verified on this machine's Mesa driver. An OpenGL ES shader path for Android is not written.
- Mesh quality changes the triangle count, not the solid.
