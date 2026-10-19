---
name: DispatchLab
description: A developer-platform console for a C++20 routing and dispatch engine on the real Tricity road network.
colors:
  background: "#fafafa"
  background-sunken: "#f2f2f2"
  surface: "#ffffff"
  surface-hover: "#f7f7f7"
  surface-active: "#f0f0f0"
  border: "#e8e8e8"
  border-strong: "#d6d6d6"
  text: "#171717"
  text-secondary: "#5c5c5c"
  text-tertiary: "#707070"
  invert-background: "#171717"
  invert-text: "#fafafa"
  accent: "#ea5a0c"
  accent-text: "#c2410c"
  accent-soft: "rgba(234, 90, 12, 0.1)"
  focus: "#0a6cff"
  ok: "#15803d"
  ok-soft: "rgba(21, 128, 61, 0.1)"
  warn: "#b45309"
  warn-soft: "rgba(180, 83, 9, 0.1)"
  err: "#c81e1e"
  err-soft: "rgba(200, 30, 30, 0.09)"
  series-dijkstra: "#2f6feb"
  series-bidirectional: "#0e9384"
  series-astar: "#7c4dde"
  series-cch: "#d6336c"
  policy-nearest: "#8a8a8a"
  policy-greedy-eta: "#ea5a0c"
  courier-idle: "#a8a8a8"
  courier-to-restaurant: "#5c5c5c"
  courier-at-restaurant: "#ca8a04"
  courier-to-customer: "#171717"
  map-restaurant: "#ea5a0c"
  map-customer: "#5c5c5c"
  map-background: "#f3f3f2"
  map-road: "#d9d9d7"
  map-arterial: "#ababa7"
  map-casing: "#ffffff"
typography:
  display:
    fontFamily: "Geist, ui-sans-serif, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "28px"
    fontWeight: 600
    lineHeight: 1.1
    letterSpacing: "-0.03em"
    fontFeature: "\"tnum\" on"
  headline:
    fontFamily: "Geist, ui-sans-serif, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "24px"
    fontWeight: 600
    lineHeight: 1.25
    letterSpacing: "-0.02em"
  title:
    fontFamily: "Geist, ui-sans-serif, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "16px"
    fontWeight: 600
    lineHeight: 1.35
    letterSpacing: "-0.01em"
  title-sm:
    fontFamily: "Geist, ui-sans-serif, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "14px"
    fontWeight: 600
    lineHeight: 1.5
    letterSpacing: "-0.005em"
  metric:
    fontFamily: "Geist, ui-sans-serif, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "18px"
    fontWeight: 550
    letterSpacing: "-0.02em"
    fontFeature: "\"tnum\" on"
  body:
    fontFamily: "Geist, ui-sans-serif, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "14px"
    fontWeight: 400
    lineHeight: 1.5
    fontFeature: "\"ss01\" on"
  body-sm:
    fontFamily: "Geist, ui-sans-serif, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "13px"
    fontWeight: 400
    lineHeight: 1.5
  label:
    fontFamily: "Geist, ui-sans-serif, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "12px"
    fontWeight: 500
    lineHeight: 1.4
  data:
    fontFamily: "Geist Mono, ui-monospace, SFMono-Regular, Menlo, monospace"
    fontSize: "12px"
    fontWeight: 400
    fontFeature: "\"tnum\" on"
rounded:
  xs: "4px"
  sm: "6px"
  md: "8px"
  lg: "12px"
  pill: "999px"
spacing:
  xxs: "4px"
  xs: "8px"
  sm: "12px"
  md: "16px"
  lg: "24px"
  xl: "32px"
  xxl: "64px"
components:
  button-primary:
    backgroundColor: "{colors.invert-background}"
    textColor: "{colors.invert-text}"
    typography: "{typography.body-sm}"
    rounded: "{rounded.sm}"
    padding: "0 12px"
    height: "32px"
  button-secondary:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.text}"
    typography: "{typography.body-sm}"
    rounded: "{rounded.sm}"
    padding: "0 12px"
    height: "32px"
  button-secondary-hover:
    backgroundColor: "{colors.surface-hover}"
  button-ghost:
    backgroundColor: "transparent"
    textColor: "{colors.text-secondary}"
    rounded: "{rounded.sm}"
    padding: "0 12px"
    height: "32px"
  button-ghost-hover:
    backgroundColor: "{colors.surface-hover}"
    textColor: "{colors.text}"
  button-small:
    rounded: "{rounded.sm}"
    padding: "0 10px"
    height: "28px"
  segmented:
    backgroundColor: "{colors.background-sunken}"
    rounded: "{rounded.sm}"
    padding: "2px"
  segmented-option:
    backgroundColor: "transparent"
    textColor: "{colors.text-secondary}"
    typography: "{typography.label}"
    rounded: "{rounded.xs}"
    padding: "0 10px"
    height: "26px"
  segmented-option-active:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.text}"
  input:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.text}"
    typography: "{typography.body-sm}"
    rounded: "{rounded.sm}"
    padding: "0 10px"
    height: "32px"
  badge:
    backgroundColor: "{colors.background-sunken}"
    textColor: "{colors.text-secondary}"
    typography: "{typography.label}"
    rounded: "{rounded.pill}"
    padding: "0 8px"
    height: "22px"
  badge-ok:
    backgroundColor: "{colors.ok-soft}"
    textColor: "{colors.ok}"
  badge-warn:
    backgroundColor: "{colors.warn-soft}"
    textColor: "{colors.warn}"
  badge-err:
    backgroundColor: "{colors.err-soft}"
    textColor: "{colors.err}"
  badge-accent:
    backgroundColor: "{colors.accent-soft}"
    textColor: "{colors.accent-text}"
  panel:
    backgroundColor: "{colors.surface}"
    rounded: "{rounded.lg}"
  panel-body:
    padding: "16px"
  nav-item:
    backgroundColor: "transparent"
    textColor: "{colors.text-secondary}"
    typography: "{typography.body}"
    rounded: "{rounded.sm}"
    padding: "0 10px"
    height: "32px"
  nav-item-hover:
    backgroundColor: "{colors.surface-hover}"
    textColor: "{colors.text}"
  nav-item-active:
    backgroundColor: "{colors.surface-active}"
    textColor: "{colors.text}"
  inspector:
    backgroundColor: "{colors.surface}"
    width: "372px"
  sidebar:
    backgroundColor: "{colors.background}"
    width: "236px"
    padding: "12px 10px"
  topbar:
    backgroundColor: "{colors.background}"
    height: "52px"
    padding: "0 24px"
  search-strip:
    backgroundColor: "{colors.border}"
    height: "168px"
  search-strip-map:
    backgroundColor: "{colors.map-background}"
  toast:
    backgroundColor: "{colors.invert-background}"
    textColor: "{colors.invert-text}"
    typography: "{typography.body-sm}"
    rounded: "{rounded.md}"
    padding: "12px 14px"
---

# Design System: DispatchLab

## Overview

**Creative North Star: "The Instrument Console"**

DispatchLab is a developer-platform console, played straight in the register of Vercel, Cloudflare and Linear dashboards. The road map is a precise instrument set inside a calm product shell. It is not a logistics control room: the shell is neutral grey, structured by hairline borders and quiet radii, and colour is spent only where it carries meaning. That means the route, the four algorithms, the dispatch policies, courier states, and pass, warn or fail status.

Density is operational. Body text is 14px, controls are 32px tall, and every number is set in tabular figures. Each view is either a scrolling page of panels (max 1240px) or a full-bleed workspace where the map fills the stage and a fixed inspector holds the controls and results. Light and dark themes are complete token sets. The theme is resolved from `localStorage` or the system preference before first paint, so the page never flashes the wrong theme.

Motion stays out of the way. State changes take 150 to 250ms with a single ease-out curve. The one authored moment is the search sweep, when four small-multiple maps reveal each algorithm's settled nodes in settle order.

**Key Characteristics:**
- Neutral greys, hairline borders, one orange accent, and complete light and dark themes.
- Geist Sans for the interface and Geist Mono for data only, both self-hosted.
- Inverted solid primary buttons. Segmented controls instead of tabs or toggles.
- A workspace layout: map stage plus a 372px inspector.
- The four-map search strip as the signature element.
- Colour as meaning, never decoration. Status is always paired with an icon or a word.

## Colors

The base is achromatic, with a single warm orange accent. On top of that sit two reserved meaning sets: four algorithm series hues, and a neutral-plus-amber ramp for courier states.

### Primary
- **Dispatch Orange** (accent): the shortest-path route line, destination pin B, restaurants on the map, the Greedy ETA policy, the busy connection dot, and the current event row (as a soft tint). It also appears as the dot in the wordmark. In dark mode it lifts to `#ff7a2e`.
- **Burnt Orange Text** (accent-text): accent text on a soft-orange fill, such as the accent badge. It is darker than the accent so it passes contrast on light surfaces.
- **Orange Wash** (accent-soft): text selection, the accent badge fill, and the highlighted current event.

### Secondary
- **Signal Blue** (focus): used only for keyboard focus rings, focused inputs and the armed map endpoint. It never marks data.

### Tertiary (algorithm series, reserved)
- **Dijkstra Blue** (series-dijkstra), **Bidirectional Teal** (series-bidirectional), **A\* Violet** (series-astar), **CCH Pink** (series-cch). These four hues mark the four algorithms everywhere: search-strip labels, table swatches, settled-node bars and chart lines. Dark mode lifts each one (`#5b93ff`, `#2ec4b0`, `#a07cf5`, `#f2648f`).

### Neutral
- **Paper** (background) / **Ink Well** (background-sunken): the app canvas, and the recessed tracks behind segmented controls, badges, command blocks and bars.
- **Card White** (surface) with **hover** and **active** steps: panels, the inspector, inputs and secondary buttons. Hover and active are near-invisible grey steps, never tints.
- **Hairline** (border) / **Strong Hairline** (border-strong): structural dividers, and the borders of controls and inputs.
- **Ink** (text), **Graphite** (text-secondary), **Ash** (text-tertiary): primary copy, then descriptions and labels, then hints, timestamps and keyboard keys.
- **Inverted Ink** (invert-background / invert-text): primary buttons, the toast and the wordmark tile. In dark mode the pair swaps to `#ededed` on `#0a0a0a`.
- **Status** (ok, warn, err, each with a soft fill): verification badges, deltas, callouts and the connection dot.
- **Policies**: Nearest is a neutral grey (policy-nearest). Greedy ETA is the brand orange (policy-greedy-eta).
- **Courier states**: idle (pale grey), heading to the restaurant (graphite), at the restaurant (amber, the only warm hue in the ramp) and heading to the customer (ink). Customers are graphite; restaurants are orange.
- **Map**: a warm off-white ground (map-background), quiet grey roads, and darker arterials with a casing in the surface colour. In dark mode the map sits near-black (`#0d0d0d`) with roads in `#303030` and arterials in `#5c5c5c`.

### Named Rules
**The Reserved Series Rule.** The four algorithm hues belong to the algorithms and nothing else. Policies, courier states, status and chrome never borrow them, and a new algorithm view reuses the same four hues.

**The One Orange Rule.** Orange is the only brand hue. It marks the route and the things a route connects (destination, restaurant), plus the Greedy ETA policy. Never use it for decoration, section fills or headings.

**The Paired Status Rule.** Status colour never stands alone. Every ok, warn or err fill carries an icon or a word, such as "4 of 4 agree on cost" with a check.

## Typography

**Display Font:** Geist (with ui-sans-serif, system-ui fallback), variable 100–900, self-hosted, OFL
**Body Font:** Geist
**Label/Mono Font:** Geist Mono (with ui-monospace, SFMono-Regular, Menlo fallback), variable, self-hosted, OFL

**Character:** A neutral grotesk set with tight negative tracking on headings and the `ss01` stylistic set throughout, paired with a mono reserved for data. The tone is a product dashboard, not an editorial page.

### Hierarchy
- **Display** (600, 28px, 1.1, -0.03em, tabular): the single headline number in a result, such as route travel time.
- **Headline** (600, 24px, 1.25, -0.02em): page titles in the page head, balanced wrapping.
- **Title** (600, 16px, 1.35, -0.01em): section headings. Panel and inspector headings drop to 14px or 13px.
- **Metric** (550, 18px, -0.02em, tabular): values in fact strips, with 12px secondary-colour units.
- **Body** (400, 14px, 1.5): the base text size. Descriptions cap at 70ch.
- **Body small** (400, 13px): tables, rows, breadcrumbs, inputs and buttons (500).
- **Label** (500, 12px): field labels, table headers, badges, legends and panel subtitles. Sentence case, no tracking.
- **Data** (Geist Mono, 11–13px, tabular): state hashes, coordinates, the replay clock, event timestamps, search-strip stats, keyboard keys and CLI commands.

### Named Rules
**The Mono Is Data Rule.** Geist Mono sets machine output only: hashes, coordinates, timestamps, commands and per-algorithm stats. Interface copy, headings and labels are always Geist Sans.

**The Tabular Figures Rule.** Every number that can change or be compared uses tabular figures. That covers table cells, facts, inputs, bars and the big result number.

## Layout

The app shell is a two-column grid: a 236px sidebar and a fluid main column (the direction contract said 232px; the build ships 236px). The sidebar holds the wordmark, the grouped navigation, the connection status card and the theme segmented control. The main column has a 52px top bar with breadcrumbs and a dataset badge, above the view.

There are two view types:
- **Page:** a centred column, max 1240px, padded 32px 24px 64px. The page head puts the title and a description capped at 70ch on the left, with actions right-aligned. Sections are spaced 32px apart. Grids use a 16px gap: 2fr/1fr (min 280px) for main plus aside, or two equal columns.
- **Workspace:** used by the map views. The map stage takes the fluid column and a fixed 372px inspector sits on the right (the contract said 360px; the build ships 372px, and 320px below 1100px). The stage stacks the map, then the search strip, then the timeline where present. Map tools sit top-right, the hint top-left and the legend bottom-left, all inset 12px.

Spacing follows a 4px rhythm: 4, 8, 12, 16, 24, 32 and 64px. Panels and inspector sections pad at 16px, table cells at 10px by 16px, and controls are 32px tall (28px small).

Responsive behaviour: below 1100px, the two-column grids stack and the inspector narrows. Below 860px the sidebar becomes an off-canvas drawer (260px, with a 30% black scrim). The workspace stacks with the map at minmax(420px, 62vh) above the inspector, the search strip becomes two by two, fact strips go to two columns, and page padding drops to 24px 16px.

## Elevation & Depth

The system is a flat hybrid. Structure comes from hairline borders and the small difference between the sunken, background and surface tones. Shadows are faint and only confirm what the border already says. Panels and floating map controls carry a barely visible ambient shadow. Only true overlays get the larger shadow: the toast and the mobile navigation drawer. Dark mode deepens both shadows rather than adding glows.

### Shadow Vocabulary
- **Hairline lift** (`box-shadow: 0 1px 2px rgba(0,0,0,0.04), 0 1px 1px rgba(0,0,0,0.02)`; dark `0 1px 2px rgba(0,0,0,0.5)`): panels, map tools, the map hint, and the active segment in a segmented control.
- **Overlay** (`box-shadow: 0 1px 2px rgba(0,0,0,0.04), 0 8px 24px -8px rgba(0,0,0,0.12)`; dark `0 1px 2px rgba(0,0,0,0.5), 0 12px 32px -10px rgba(0,0,0,0.7)`): the toast and the off-canvas sidebar.
- **Focus halo** (`box-shadow: 0 0 0 3px` focus at 16–20% mix): focused inputs and the armed endpoint. Status dots use the same 3px halo in their soft colour.

### Named Rules
**The Border First Rule.** Depth is drawn with 1px borders and tone steps. If an element needs a shadow to be told apart from its neighbour, fix its border or its tone instead.

## Shapes

The corners are quiet and nest by size: 4px for the smallest pieces (segment options, keyboard keys, hashes), 6px for controls (buttons, inputs, nav items, segmented tracks, map tools), 8px for small containers (the connection card, callouts, toast, heat grid, split-hash grid), and 12px for panels and empty states. Badges are full pills. Swatches are 2px squares, legend dots are circles, and route legends are 16 by 2px lines. Grids of cells, such as the search strip, heat map and split hashes, use a 1px gap over the border colour, so the gaps draw the hairlines. Empty states are the only dashed border.

## Components

### Buttons
Compact and quiet. The primary action is the only filled one.
- **Shape:** gently rounded (6px), 32px tall, 0 12px padding, 13px/500 label, 15px icon with an 8px gap.
- **Primary:** inverted solid, ink on light and near-white on dark, with a matching border. On hover the opacity drops to 0.88.
- **Secondary (default):** surface fill with a strong hairline border. On hover the fill goes to the hover tone and the border darkens to tertiary text.
- **Ghost:** transparent and secondary-coloured. On hover it gains the hover fill and ink text. Used for icon buttons and the menu button.
- **States:** pressed nudges 0.5px down. Disabled sits at 45% opacity. Loading shows a 12px spinner in the current colour. Transitions run at 150ms ease-out.

### Segmented control
The selector for modes, traffic levels, policies and theme.
- **Track:** sunken fill, hairline border, 6px radius, 2px inset and gap.
- **Option:** 26px tall, 12px/500, secondary text. The active option is raised onto the surface fill with ink text and the hairline lift shadow.

### Chips / Badges
- **Style:** a 22px pill, 12px/500, sunken fill with a hairline border for neutral. The status variants (ok, warn, err, accent) use a soft fill and coloured text, with no border, and an optional 12px icon.

### Cards / Containers
- **Corner Style:** 12px.
- **Background:** surface.
- **Shadow Strategy:** hairline lift (see Elevation & Depth).
- **Border:** 1px hairline. The head and foot are divided by hairlines.
- **Internal Padding:** 14px by 16px in the head and 16px in the body. Fact strips are hairline-divided cells with label above metric. Definition rows are 9px by 16px, with the label on the left and the tabular value on the right.

### Inputs / Fields
- **Style:** 32px, strong hairline border, surface fill, 6px radius, 13px tabular text. The label (12px/500) sits 6px above the field and the hint is 12px tertiary. Selects use a custom chevron.
- **Focus:** the border turns focus blue, with a 3px halo at 20%.
- **Error:** the border turns err red (`aria-invalid`).

### Navigation
- **Sidebar items:** 32px rows, 6px radius, a 16px line icon, 450-weight secondary text. On hover they get the hover fill and ink text. The current page gets the active fill, ink text and weight 550. Group labels are 12px tertiary text in sentence case. On mobile the sidebar becomes an off-canvas drawer that slides in over 220ms.
- **Top bar:** 52px with breadcrumbs (13px, slash separators in the strong border colour, current page in 550 ink).

### Icons
These are a custom 16px line set with a 1.5 stroke and round joins, drawn for this console and inlined as SVG. They are sized at 16px in navigation, 15px in buttons and map tools, and 12–14px in badges and segments.

### Inspector
This is the right column of the workspace: a surface fill with a left hairline, stacked into 16px sections divided by hairlines, each with a 13px heading. The endpoint rows show a 20px circular pin: A is inked, B is orange. An endpoint waiting for a map click is "armed" with the focus border and halo. Results lead with the display number, then the agreement badge, then a compact table with per-algorithm bars.

### Search strip (signature)
This is the signature element. Four small-multiple maps run edge to edge along the bottom of the routing stage (168px tall; two by two on mobile). They are separated by 1px hairline gaps, and each sits on the map ground. Every tile carries a translucent surface label with the algorithm's swatch and name, plus a mono stat (nodes settled, latency). After a query, all four tiles sweep together over 1.5s, revealing the settled nodes in settle order. This shows four algorithms reaching the same answer after exploring very different amounts of the city. Clicking a tile selects it, shown by a 2px inset ink ring.

### Map
This is a canvas instrument on the map ground: roads, cased arterials and the orange shortest path in a casing. Overlays are floating surface chips: a stacked tool column, a hint and a translucent legend. The cursor shows grab, grabbing or crosshair to match the current mode.

### Motion
- **State changes:** 150ms (the table row hover is 120ms) on `cubic-bezier(0.16, 1, 0.3, 1)`.
- **Overlays:** the toast rises 8px over 200ms. The scrim fades over 200ms and the drawer slides over 220ms.
- **Entrance:** `fade-in` moves an already-visible element up from 4px over 240ms, by position only, and never animates from opacity 0.
- **Data:** bar widths ease over 400ms. Loading uses a 1.4s shimmer and a 700ms spinner.
- **Authored moment:** the 1.5s search sweep. Nothing else in the system is choreographed.
- **Reduced motion:** CSS transitions and animations collapse to 1ms, and the routing search sweep jumps straight to its final state.

### Named Rules
**The Visible Default Rule.** Entrances never start from opacity 0. Content is visible at rest and only settles into position.

**The One Sweep Rule.** The search sweep is the only authored animation. Every other motion is a 150–250ms ease-out state change.

## Do's and Don'ts

### Do:
- **Do** make the primary action an inverted solid button (ink on light, near-white on dark), with one per context.
- **Do** use the segmented control (sunken track, raised active segment) for every choice among two to five modes.
- **Do** keep the four algorithm hues for the algorithms only, and colour policies grey (Nearest) and orange (Greedy ETA).
- **Do** set hashes, coordinates, timestamps and commands in Geist Mono, and every comparable number in tabular figures.
- **Do** build map views as a workspace: the map stage plus a 372px inspector, with floating surface chips over the canvas.
- **Do** draw structure with 1px hairlines and tone steps, using 6px radii for controls and 12px for panels.
- **Do** define every new colour for both themes on `:root` and `[data-theme="dark"]`.
- **Do** keep state transitions to 150–250ms on the single ease-out curve.

### Don't:
- **Don't** build a logistics "control room": no dark neon maps, glowing routes or gauge cards.
- **Don't** reuse an algorithm series hue for a policy, a courier state, a status or chrome.
- **Don't** use orange for decoration, headings or section fills.
- **Don't** use Geist Mono for interface copy, headings or labels.
- **Don't** fade entrances in from opacity 0, and don't add authored animation beyond the search sweep.
- **Don't** rely on colour alone for status. Pair it with an icon or a word.
- **Don't** load fonts, tiles or scripts from external hosts. Everything is self-hosted and must open from `file://`.
