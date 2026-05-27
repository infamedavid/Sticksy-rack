# Sticksy

Sticksy is a decorative SVG sticker plugin for VCV Rack.

It provides blank panel modules that let you place visual stickers on your patch for decoration, signatures, labels, or visual organization. It does not process audio, CV, or DSP.

## Modules

- Sticksy Blank 3HP
- Sticksy Blank 5HP
- Sticksy Blank 9HP
- Sticksy Blank 12HP

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
