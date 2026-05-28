# Sticksy

Sticksy is a decorative SVG sticker plugin for VCV Rack.



- Sticksy Blank 3HP
- Sticksy Blank 5HP
- Sticksy Blank 9HP
- Sticksy Blank 12HP
- Sticksy Flipbook 12HP

## How it works

1. Right-click a Sticksy module.
2. Choose a background.
3. Choose a storage mode.
4. Choose **Single** or **Multiple** before loading stickers.
5. Select **Load SVG...** and pick an `.svg` file.

### Single mode

- One sticker is shown, centered on the panel.
- Loading another SVG replaces the current sticker.

### Multiple mode

- Each loaded SVG is added as a new sticker.
- **Shake** randomizes sticker position and rotation.
- Loaded SVGs appear in the context menu list.
- Each listed sticker can be deleted individually.

### Mode switching rule

- Mode cannot be changed while stickers are loaded.
- Delete loaded stickers first, then switch mode.

## Visual behavior

- User SVGs are never scaled.
- Prepare SVGs at your desired display size.
- Larger SVGs are clipped by the module panel.
- Stickers do not draw outside panel bounds.
- In Multiple mode, stickers can overlap and can be partially clipped.

## Storage behavior

- **Referenced**: stores the original file path in the patch.
- **Save in Sticksy Library**: copies the SVG to Sticksy’s user folder.
- SVG content is not embedded in the patch.
- If a sticker file is missing or invalid, Sticksy uses the `Sticksy.svg` fallback.

## Sticksy Flipbook

Sticksy Flipbook is a clocked SVG flipbook module. It is decorative and does not process audio.

- Width: **12HP**
- Input: **CLK**
- Output: **EOC**

### Loading frames

- Right-click and choose **Load Flipbook Image...**
- If the selected file ends with a numeric suffix, Sticksy Flipbook auto-detects a sequence in the same folder.
- Examples:
  - `dog01.svg`, `dog02.svg`, `dog03.svg`
  - `frame1.svg`, `frame2.svg`, `frame10.svg` (numeric ordering)
- If there is no numeric suffix, only the selected SVG is loaded.
- Maximum sequence length is **128 frames**.

### Play Modes

- **Forward**
- **Reverse**
- **Ping Pong**
- **Random**

### EOC

- **EOC** emits a short pulse when a cycle completes according to the selected Play Mode.

### Optional static background

- **Load Background Image...** loads one static SVG background.
- **Clear Background** removes it.
- Background is drawn behind the current frame.

### Sizing and clipping

- SVGs are centered at real size (no scaling).
- Oversized SVGs are clipped to module bounds.
- Prepare SVGs at your intended visual size.
