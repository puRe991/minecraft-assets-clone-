# Grafikqualität auf Stand 2026 — Analyse und Vorschläge

Dieses Dokument ist ein **Vorschlagskatalog, keine Implementierung**. Es beschreibt,
wo die Bildqualität von MiniCraft heute steht, was sie limitiert, und welche
Schritte in welcher Reihenfolge den größten Qualitätsgewinn pro Aufwand bringen.

Grundlage ist der aktuelle Stand von `src/main.c` (Software-Raycaster, Win32/GDI,
480×270 interne Auflösung, 16×16-Texturen).

---

## 1. Bestandsaufnahme — was das Bild heute limitiert

| Bereich | Ist-Zustand | Auswirkung |
|---|---|---|
| Beleuchtung | 4 feste Konstanten pro Flächenrichtung (`1.0 / 0.8 / 0.68 / 0.55`, `main.c:731`) | Keine Sonne, keine Schatten, kein AO — die Welt wirkt flach und „gebacken" |
| Farbraum | Alle Rechnungen direkt auf sRGB-Bytes | Abdunklung und Fog sind physikalisch falsch → matschige Verläufe |
| Texturfilterung | Point Sampling, kein Mip (`main.c:724-726`) | Starkes Flimmern/Moiré in der Ferne — der auffälligste „alt"-Effekt |
| Texturauflösung | Alles wird hart auf 16×16 heruntergerechnet (`load_one`, `main.c:409-416`) | Hochauflösende Packs bringen nichts |
| Alpha | Wird beim Laden verworfen (`| 0xff000000u`, `main.c:415`) | Kein Glas, kein Laub-Cutout, keine Transparenz |
| Wasser | `stops_ray()` schließt Wasser aus (`main.c:135`) — es wird **nie getroffen und nie gezeichnet** | Wasser ist derzeit komplett unsichtbar |
| Kantenglättung | Keine, exakt ein Strahl pro Pixel | Harte, kriechende Treppenkanten in Bewegung |
| Upscaling | `StretchDIBits` mit `COLORONCOLOR` = Nearest (`main.c:1063`) | Bei adaptiver Auflösung wird das Bild klotzig statt weich |
| Himmel | Vertikaler 2-Farb-Verlauf (`view_setup`, `main.c:813-819`) | Keine Sonne, keine Wolken, kein Tageszeitwechsel, sichtbares Banding |
| Fog | Linear gegen eine **feste** Farbe (`SKY_BOT_*`) | Horizont und Nebel driften farblich auseinander |
| Zielmarkierung | Nur XOR-Fadenkreuz | Der anvisierte Block ist nicht hervorgehoben — Bau-Feedback fehlt |
| HUD | XOR-Kreuz + Nearest-skalierte Hotbar, Status im Fenstertitel | Wirkt wie eine Techdemo, nicht wie ein Spiel |
| Seitenverhältnis | Fest auf `RENDER_W/RENDER_H` = 16:9 (`main.c:809`) | Beim Resizen des Fensters wird das Bild verzerrt |
| Weltgröße | 128×64×128, Sichtweite 96 | Man sieht die gesamte Welt — es gibt keinen echten Horizont |

**Kernbefund:** Der Flaschenhals ist nicht die Geschwindigkeit — der Renderer
läuft nach der Performance-Optimierung bei ~196 FPS. Der Flaschenhals ist, dass
das eingesparte Frame-Budget nie in Bildqualität reinvestiert wurde. Es ist
Rechenzeit für Schatten, Filterung und Antialiasing vorhanden.

---

## 2. Phase 1 — Größter Effekt, alles im bestehenden Renderer

Diese fünf Punkte verändern das Bild am stärksten und brauchen weder GPU noch
Architekturumbau. Sie sind bewusst nach Wirkung/Aufwand sortiert.

### 2.1 Linearer Farbraum + Tonemapping + Dithering

Heute wird `Farbe × Lichtfaktor` direkt auf sRGB-Bytes gerechnet. Das ist der
Grund, warum Schattenseiten „schmutzig" statt dunkel aussehen und der
Fog-Übergang stumpf wirkt.

- sRGB→Linear über eine 256-Einträge-LUT beim Texturladen (einmalig, kostet zur
  Laufzeit nichts).
- Beleuchtung, Fog, Blending in Linear rechnen.
- Am Ende ein Tonemapper (ACES-Fit oder Reinhard-Jodie) + Linear→sRGB über LUT.
- Vor der 8-Bit-Ausgabe **Ordered/Blue-Noise-Dither** — der Himmelsverlauf
  bandet aktuell sichtbar; Dithering kostet praktisch nichts und beseitigt es.

*Aufwand: klein. Wirkung: hoch. Laufzeitkosten: ~0.* Diese Änderung ist die
Voraussetzung für alles Weitere (HDR-Himmel, Bloom, Belichtung).

### 2.2 Voxel-Ambient-Occlusion + gerichtete Sonne + Schattenstrahl

Drei Stufen, einzeln einführbar:

1. **Voxel-AO** (die klassische Minecraft-Eckenverdeckung): pro Treffer die drei
   Nachbarblöcke jeder Flächenecke abfragen, bilinear über die Fläche
   interpolieren. Kostet 4 Blocklesezugriffe pro Pixel, keine Strahlen.
   *Das ist der Effekt, der Voxelwelten „massiv" statt „aus Pappe" aussehen lässt.*
2. **Gerichtete Sonne** mit N·L statt der vier Konstanten, plus Sonnenstand als
   Funktion der Tageszeit. Kosten: null.
3. **Harte Sonnenschatten** über einen zweiten DDA-Strahl vom Trefferpunkt zur
   Sonne. Der Schattenstrahl darf beim ersten Treffer abbrechen und braucht
   keine Texturarbeit — mit dem vorhandenen Makrogitter (`g_macro`) ist er
   deutlich billiger als der Primärstrahl. Realistisch +40–70 % Frame-Zeit.
4. **Weiche Schatten** danach quasi gratis: Sonnenrichtung pro Pixel innerhalb
   der ~0.53°-Sonnenscheibe jittern und das Rauschen von TAA (2.5) wegmitteln
   lassen → echte Halbschatten.

*Optimierung:* Da die Sonne sich langsam bewegt, kann die Sichtbarkeit pro
Blockfläche gecacht und nur bei Blockänderung/Sonnenbewegung invalidiert werden.

### 2.3 Mipmaps + bilineare/trilineare Filterung + Blockumrandung

- Pro Textur eine Mip-Kette (16→8→4→2→1) vorberechnen.
- Mip-Level aus der Strahlfußabdruck-Breite wählen
  (`level ≈ log2(dist × Pixelwinkel × TEX)`) — die Distanz liegt bereits vor.
- Bilinear innerhalb, trilinear zwischen den Leveln. Für flache Blickwinkel auf
  Böden eine günstige 2-Tap-Anisotropie entlang der dominanten Achse.
- **Blockumrandung**: Der anvisierte Block bekommt einen Drahtrahmen. Das ist
  minimaler Aufwand (der Treffer wird für `do_break`/`do_place` ohnehin
  berechnet) und der spürbarste Usability-Gewinn im ganzen Katalog.

*Anmerkung:* Filterung ist der Hauptgrund, warum das Bild „2010" aussieht —
nicht die Geometrie. Stabile, gefilterte Texel sind der größte Einzelposten.

### 2.4 Echte Transparenz: Wasser, Glas, Laub

Aktuell wird Wasser gar nicht gerendert und Alpha beim Laden verworfen. Das
verlangt eine verallgemeinerte Traversierung, die nicht beim ersten Treffer
endet, sondern transparente Treffer sammelt und von hinten nach vorne mischt.

- **Wasser**: Fresnel-gewichtete Reflexion (ein Sekundärstrahl oder ersatzweise
  nur Himmelsfarbe), Beer-Lambert-Absorption über die durchlaufene Wassertiefe
  (blaugrüne Tiefenverfärbung), Wellennormalen aus 2 Gerstner-/Sinus-Oktaven.
- **Refraktion**: Der transmittierte Strahl wird an der Oberfläche gebrochen
  (n = 1.33). In einem Raycaster ist das nahezu kostenlos — im Gegensatz zur
  Rasterisierung, wo es ein Hack ist. Das ist ein Effekt, den man 2026 erwartet
  und der hier billiger zu haben ist als anderswo.
- **Unterwasser**: Fogfarbe umschalten + Kaustiken (scrollendes Muster,
  moduliert mit der Sonnensichtbarkeit).
- **Glas**: Alpha-Blending mit Fresnel statt des heutigen opaken Rahmens.
- **Laub-Cutout**: Alpha-Test während der Traversierung (Treffer mit α < 0.5
  wird übersprungen). Bäume hören auf, Würfelklötze zu sein — großer Gewinn für
  die Silhouette der Welt.

### 2.5 Temporales Antialiasing und temporales Upscaling

Der Renderer ist deterministisch ohne Sub-Pixel-Jitter, also kriechen alle
Kanten. Vorschlag:

- Sub-Pixel-Offset pro Frame über eine Halton(2,3)-Folge jittern.
- Vorheriges Frame reprojizieren. **Besonderheit hier:** In einem Voxel-Raycaster
  ist der Welt-Trefferpunkt exakt bekannt — die Reprojektion ist mathematisch
  exakt, nicht approximiert wie bei Rasterisierung mit Motion-Vector-Buffer.
- Neighbourhood-Clamping (3×3-AABB in YCoCg) gegen Ghosting.
- Damit wird TAA gleichzeitig zum **Denoiser** für die stochastischen Effekte
  aus 2.2 (weiche Schatten), 2.4 (raue Reflexionen) und 4.x (GI).
- Das gleiche History-Buffer ersetzt `StretchDIBits`-Nearest durch ein
  temporales Upscaling („DLSS für Arme"): niedrig rendern, hochauflösend
  akkumulieren. Die adaptive Auflösung kostet dann kaum noch Schärfe.

*Einfachere Alternative, falls TAA zu riskant erscheint:* kantenselektives
Supersampling — nur dort 4 zusätzliche Strahlen schießen, wo Nachbarpixel
unterschiedliche Blöcke/Flächen treffen. Kostet ~5–10 % und beseitigt den
Großteil der Treppen ohne Ghosting-Risiko.

---

## 3. Phase 2 — Atmosphäre, Material, Präsentation

### 3.1 Physikalischer Himmel und Atmosphäre
- Preetham- oder Hosek-Wilkie-Fit für Rayleigh-/Mie-Streuung, pro Frame in eine
  kleine LUT vorberechnet und über die Blickrichtung indiziert → echte
  Sonnenauf-/untergangsfarben statt eines 2-Farb-Verlaufs.
- Sonnenscheibe mit Randverdunklung, Mond, Sterne bei Nacht.
- Der Himmel wird zur **Lichtquelle** für die Umgebungsbeleuchtung — damit
  stimmt die Ambient-Farbe automatisch zur Tageszeit.
- Fog nicht mehr gegen eine Konstante, sondern gegen die Himmelsfarbe **in
  Strahlrichtung** blenden → der Horizontübergang wird nahtlos.

### 3.2 Volumetrischer Nebel und Lichtstrahlen
Der Primärstrahl wird in ~16 Schritten marschiert und pro Schritt die
Sonnensichtbarkeit abgefragt (Blue-Noise-Offset, TAA glättet). Das ist der
Effekt, der am unmittelbarsten als „modern" gelesen wird — Sonnenstrahlen durch
Baumkronen, Bodennebel über Wasser.

### 3.3 Wolken und Wetter
- Volumetrische Wolken als zweite, grobe Voxel-/3D-Noise-Schicht über der Welt,
  raymarched. Passt strukturell perfekt zur vorhandenen Engine.
- Regen/Schnee als Partikel plus nasse Oberflächen (dunkler, glänzender).

### 3.4 PBR-Materialsystem und Kompatibilität mit modernen Resource-Packs
Das ist der konkrete Hebel, um „Standard 2026" ohne eigene Kunstproduktion zu
erreichen:

- Loader auf die etablierten Konventionen erweitern: **LabPBR** (`_n` Normal,
  `_s` Specular — Iris/OptiFine) bzw. die **RTX-MER**-Konvention
  (`_normal`/`_mer`). Damit rendert das Spiel vorhandene High-End-Packs.
- Shading mit GGX-Specular, Roughness, Metallic, Emissive.
- Glanzreflexionen = ein Spiegelstrahl mit roughness-abhängigem Jitter,
  entrauscht durch TAA.
- **Parallax Occlusion Mapping** über die Height-Map: nach dem Flächentreffer
  das Höhenfeld im Texturraum marschieren. In einem Raycaster ist das exakt
  machbar und gibt Kopfsteinpflaster/Ziegeln echte Tiefe.
- `TEX` von der festen 16 lösen (Atlas mit variabler Kachelgröße, 64² oder 128²),
  sonst verpufft jedes hochauflösende Pack.

### 3.5 Post-Processing
- **Bloom** auf Emissive und Sonne (Downsample-Kette + Gauß; bei 480×270 billig).
- **Auto-Belichtung** über ein Luminanz-Histogramm → Höhlen sind dunkel, das
  Heraustreten blendet.
- **Color-Grading-LUT** (16³), damit ein „Look" gestaltbar wird.
- **CAS-artiges Sharpening** nach dem Upscaling, ergänzt 2.5.
- Optional und dezent: Vignette, leichte Bewegungsunschärfe.

### 3.6 HUD und UI
- Echter Bitmap-Font-Atlas statt Status im Fenstertitel; F3-artiges Debug-Overlay.
- Alpha-gemischte UI mit Schlagschatten statt XOR.
- Hotbar-Icons als isometrische 3D-Blockvorschau statt flacher Seitentextur.
- Weiches Fadenkreuz, animierte Slot-Auswahl, Blockbruch-Fortschrittsanimation.

### 3.7 Kachel-Wiederholung brechen
Ein unterschätztes „Alt"-Signal: sichtbare Wiederholung derselben 16×16-Kachel
über ganze Flächen. Günstige Gegenmittel:
- Blockkoordinaten hashen und daraus 1 von 4 Rotationen/Varianten für Ober-
  flächen wählen.
- Connected Textures für Glas/Blöcke gleicher Art.
- Leichte Farbvariation pro Block aus einem Rauschwert.

Kostet fast nichts und verändert den Gesamteindruck erstaunlich stark.

---

## 4. Phase 3 — Der eigentliche Sprung: Backend-Entscheidung

Realistisch ist die Obergrenze eines reinen CPU-Raycasters bei 480×270
(hochskaliert auf 960×540) deutlich unter dem, was 2026 erwartet wird. Drei
Wege, mit ehrlichen Trade-offs:

### Option A — CPU behalten, klüger machen
SIMD-Paket-Tracing (SSE2 für die 32-Bit-Variante, AVX2 für 64-Bit; 4 bzw. 8
Strahlen gleichzeitig), plus alles aus Phase 1/2.

- **Pro:** null Abhängigkeiten, XP-/32-Bit-Versprechen bleibt, ein Übersetzungs-
  modul, Build bleibt wie er ist.
- **Contra:** Kein GI, kein 1080p60 mit Schatten auf schwacher Hardware.
- **Realistisches Ziel:** 1280×720 intern bei 60 FPS mit Sonnenschatten, AO und
  gefilterten Mips auf einem modernen 8-Kerner.

### Option B — OpenGL-3.3-Backend, Software-Renderer als Fallback *(Empfehlung)*
Dieselbe DDA-Traversierung als Compute-/Fragment-Shader. Der Traversierungskern
in `raycast()` ist bereits so isoliert, dass er fast wörtlich nach GLSL portiert
werden kann; die Welt geht als 3D-Textur, das Makrogitter als zweite, kleinere
3D-Textur (bzw. eine Mip-Pyramide daraus).

- **Pro:** 1080p bei 144 FPS mit GI, Schatten und Volumetrics realistisch.
  Braucht nur `opengl32.dll` + `wglGetProcAddress` — **weiterhin keine externen
  Bibliotheken**, weiterhin MinGW-Cross-Build, weiterhin eine `.exe`.
  Mit erhaltenem Software-Pfad (`--software`) geht das „läuft ohne GPU"-
  Versprechen nicht verloren.
- **Contra:** Zwei Renderpfade müssen gepflegt werden.

### Option C — D3D12/Vulkan-Compute-Pathtracer mit ReSTIR
Echte 2026-Technik (das, was Minecraft RTX und Teardown machen).

- **Pro:** Referenzqualität — mehrfach gestreutes Licht, korrekte Emissive-Blöcke.
- **Contra:** Großer Umbau, verliert die Alt-Hardware-Unterstützung vollständig.

**Empfehlung:** Option B. Sie erhält alle Alleinstellungsmerkmale des Projekts
(eine Datei, keine Abhängigkeiten, läuft ohne GPU) und hebt gleichzeitig die
Qualitätsdecke um eine Größenordnung.

**Unabhängig davon:** Auch im Software-Modus sollte die Ausgabe nicht mehr über
`StretchDIBits` laufen. Ein texturiertes Quad (GL oder D3D) gibt gefiltertes
Upscaling, VSync-Kontrolle und Sharpening quasi gratis — GDI wird bei höheren
Auflösungen selbst zum Flaschenhals, weil der Blit auf der CPU passiert.

---

## 5. Weltdetail und Geometrie

Bildqualität ist nicht nur Shading. Aktuell sieht man bei 128×64×128 Blöcken und
96 Blöcken Sichtweite die *gesamte* Welt — es gibt keinen Horizont.

- **Chunk-Streaming** statt fester Weltgröße, mit Sichtweiten von 256–512 Blöcken.
- **Hierarchische Beschleunigungsstruktur** statt des festen 8³-Makrogitters:
  Sparse-Voxel-Octree oder Brickmap. Bringt Qualität *und* Geschwindigkeit.
- **Voxel-LOD**: entfernte Chunks aus einer niedriger aufgelösten Mip-Stufe der
  Voxeldaten tracen — mit derselben Pyramide, die auch die Traversierung
  beschleunigt.
- **3D-Rauschen** für Höhlen und Überhänge statt reiner Heightmap.
- **Biome** mit eigenen Tints und Materialsätzen (die Tint-Infrastruktur in
  `g_texmap` existiert bereits).
- **Sub-Block-Geometrie**: Stufen, Platten, Zäune.
- **Lichtausbreitung für Emissive-Blöcke** (Fackeln, Lava): 4-Bit-Flood-Fill pro
  Kanal wie in Minecraft, inkrementell bei Blockänderung. Billig und liefert
  farbiges lokales Licht — Basis für die GI-Bounce aus Phase 3.

---

## 6. Asset-Pipeline

- Das mitgelieferte Beispiel-Pack (`tools/export_assets.c`) auf 64² oder 128²
  anheben und **PBR-vollständig** exportieren: Albedo + Normal + Roughness/
  Metallic/Emissive. Der Generator existiert bereits, es fehlen nur die
  zusätzlichen Kanäle.
- `.mcmeta`-Animationen parsen (Frametime), damit Wasser und Lava fließen —
  aktuell wird bei Animationsstreifen nur das erste Bild genutzt
  (`main.c:408`).
- Alphakanal beim Laden erhalten statt zu verwerfen.
- Optional: Höhenkarten aus Albedo ableiten, falls ein Pack keine mitbringt,
  damit POM auch mit Vanilla-Packs funktioniert.

---

## 7. Kleinere Korrekturen mit sichtbarer Wirkung

Diese fallen in die Kategorie „schnell erledigt, sofort sichtbar":

1. **Seitenverhältnis aus der echten Fenstergröße** statt fest `RENDER_W/RENDER_H`
   (`main.c:809`) — sonst verzerrt jedes nicht-16:9-Fenster das Bild.
2. **Fog gegen die Himmelsfarbe in Strahlrichtung** statt gegen die Konstante
   `SKY_BOT_*`.
3. **Dithering des Himmelsverlaufs** gegen sichtbares Banding.
4. **Blockumrandung** für den anvisierten Block.
5. **Fadenkreuz** als gezeichnetes Sprite mit Kontrastrand statt XOR.
6. **Hotbar-Icons** korrekt gefiltert skalieren statt Nearest.

---

## 8. Vorgeschlagene Reihenfolge

| Stufe | Inhalt | Erwarteter Qualitätssprung | Risiko |
|---|---|---|---|
| 1 | Linearer Farbraum, Tonemapping, Dither (2.1) | mittel | sehr gering |
| 2 | Voxel-AO + Sonne + harte Schatten (2.2) | **sehr hoch** | gering |
| 3 | Mipmapping + Filterung + Blockumrandung (2.3) | **sehr hoch** | gering |
| 4 | Wasser, Glas, Laub-Cutout (2.4) | hoch | mittel (Traversierungsumbau) |
| 5 | TAA + temporales Upscaling (2.5) | hoch | mittel (Ghosting-Tuning) |
| 6 | Himmelsmodell, Volumetrics, Bloom, Belichtung (3.1–3.5) | hoch | mittel |
| 7 | PBR-Pack-Support, größere Texturen (3.4) | hoch | mittel |
| 8 | HUD/Font, Kachelvariation (3.6, 3.7) | mittel | gering |
| 9 | GPU-Backend (Option B) | **sehr hoch** | hoch |
| 10 | GI-Bounce, Reflexionen, Denoiser (auf GPU) | sehr hoch | hoch |

Nach Stufe 5 sieht das Spiel bereits wie ein modernes Voxelspiel mit
Shader-Pack aus. Ab Stufe 9 ist echte 2026-Referenzqualität erreichbar.

---

## 9. Zielkonflikte, die vorab zu entscheiden sind

1. **„Läuft auf alter Hardware" vs. Fidelität 2026.** Beides gleichzeitig geht
   nur über explizite Qualitätspresets (Low = heutiger Look, Ultra = Phase 3).
   Das sollte vor Beginn festgelegt werden, weil es die Architektur bestimmt.
2. **32-Bit/XP-Support** begrenzt SIMD auf SSE2 und den Adressraum auf ~2 GB.
   Größere Welten (Abschnitt 5) kollidieren damit — ggf. nur im 64-Bit-Build.
3. **„Keine externen Bibliotheken"** bleibt bei Option B erhalten
   (`opengl32.dll` ist Systembestandteil), bei Option C nicht mehr zwingend.
4. **TAA braucht History-, Tiefen- und Motion-Buffer.** Bei 480×270 ist das
   speichertechnisch irrelevant, bei 1080p nicht mehr — Speicherbudget mit
   einplanen.
5. **Schatten verdoppeln grob die Strahlkosten.** Der vorhandene
   Performance-Vorsprung (196 FPS gemessen) reicht dafür aus, aber das
   Performance-Kapitel im README wäre danach neu zu vermessen.
