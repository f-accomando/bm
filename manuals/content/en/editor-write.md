title: bm Write: documents
accent: Write
lang: en
kicker: A bm editor
subtitle: Write letters and reports on the console, save them on the SD card and export them as PDF, HTML, Markdown or text.
footer: bm manuals - editor-write

::: readme
**What this is.** The manual of bm Write, the word processor of the bm suite.

**How to use it.** Read the first three chapters once, then follow the numbered steps of the tutorials in order. Anything in a bordered box with a shadow is meant to be copied as it is.

**What is missing.** Every gap is marked with a highlighted TO FILL tag; nothing is invented.
:::

# What bm Write is

bm Write is an app for writing documents. You type with a keyboard, or with the pad, and you see the text on A4 pages exactly as they will print.

| Format | Extension | What it is |
|---|---|---|
| bm Write document | `.BMD` | the format you save and edit; plain text inside |
| PDF | `.PDF` | export, same lines on the same pages as the screen |
| HTML | `.HTM` | export |
| Markdown | `.MD` | export; it can also be opened |
| Text | `.TXT` | export; it can also be opened |

## What it does

- Seven paragraph styles: title, two headings, body, quote, bullet list and numbered list.
- Bold, italic and underline on single letters.
- Four alignments: left, centre, right and justify.
- Pages of A4 size with 2 cm margins and the page number at the bottom. The status line shows `Page 1 of 3` and the number of words.
- Word completion as in bm Code, in Italian or in English.
- Undo and redo, copy, cut and paste.

## Why it matters

The PDF is made by the console itself. It has the same lines on the same pages as the screen, so what you see is what you print. The font is Courier, whose letters are all of the same width, as the screen's are.

## Good to know

- Your documents live in the folder `/docs` on the SD card. They are shared by the apps that you allow to use them.
- The names are in the 8.3 form, without folders: up to 8 letters or digits, like `LETTER.BMD`.
- bm Write is a Market app: it is not part of the kernel.

# First start

## The question about documents

The first time an app uses your documents, the console asks you whether you allow it, as it does for the network. Your answer is kept in the file `bm/config.txt` on the SD card.

- If you say yes, bm Write can list, read, write and delete files in `/docs`.
- If you say no, saving, opening and exporting do not work: every call fails and the status line shows the reason.

[TO FILL: the exact words of the question on the screen and the buttons to answer it]

## The first screen

You see an empty page on a dark desk. The cursor is a blinking bar. The toolbar at the top says `bm Write`, then `Untitled`. Start typing: the text appears on the page.

If you left a document with changes the last time, bm Write brings it back with the message `the document with its changes is back (Ctrl+S saves it)`. If you left a saved document, it opens it again.

::: note
Press **F1** at any time to see the keys on the screen. Any key closes the list.
:::

![bm Write at the start: an empty page, the toolbar above and the status line below](shots/editor-write-start.png)

# The screen and the keys

## The screen

| Part | Where | What it shows |
|---|---|---|
| Toolbar | top line | `bm Write`, the document name (`Untitled` if new, with `*` if it has changes), the style of the paragraph, the letters `B` `I` `U` and `L` `C` `R` `J` |
| Pages | the middle | the document on A4 pages, the cursor, the selection and the grey-blue word that Tab would write |
| Status line | bottom line | `Page N of M`, the number of words, `PAD` and the pad mode when pad typing is on, the language |

On the toolbar, the letters that are lit show the state at the cursor: bold, italic, underline, and the alignment of the paragraph. Messages such as `saved /docs/LETTER.BMD` take the place of the status line for a few seconds.

## Writing and moving

| Key | What it does |
|---|---|
| letters, digits, symbols | type at the cursor; a selection is replaced |
| Enter | new paragraph |
| Backspace, Del | delete backwards, forwards |
| Tab | write the grey-blue word; with no word, four spaces |
| Arrows, Home, End, PgUp, PgDn | move |
| F3 | start or end a selection |
| Shift + arrows | select |
| Esc | with a selection: drop it; otherwise open the menu |

## Look and edit

| Key | What it does |
|---|---|
| F2 | next style of the paragraph |
| F4 | next alignment of the paragraph |
| Ctrl+B, Ctrl+I, Ctrl+U | bold, italic, underline |
| Ctrl+L, Ctrl+E, Ctrl+J | align left, centre, justify |
| Ctrl+Z, Ctrl+Y | undo, redo |
| Ctrl+C, Ctrl+X, Ctrl+V | copy, cut, paste |
| F1 | the list of keys |

## Files

| Key | What it does |
|---|---|
| Ctrl+S | save; asks for a name the first time |
| Ctrl+Shift+S | save as, with a new name |
| Ctrl+O | open a document from `/docs` |
| Ctrl+N | new document |
| Esc | the menu, with `Export...` |

## The menu

Press **Esc** (or **Start** on the pad). Use the up and down arrows and Enter. The entries are: New document, Open..., Save, Save as..., Export..., Style, Alignment, Bold, Italic, Underline, Select, Undo, Redo, Word completion, Language, Pad typing, Keys, Exit. `Word completion` switches the grey-blue words on and off. `Language` changes the language of the words between Italiano and English.

## The pad

| Button | What it does |
|---|---|
| DPAD | move (it repeats if held) |
| confirm button | new paragraph |
| back button | delete |
| Start | the menu |
| Share | pad typing on or off |

The key screen (F1) says "Share writes with the pad" and the menu says Share for pad typing; only the list shown while F12 is held says SELECT.

[TO FILL: which physical button writes a new paragraph and which deletes, on the Pi Zero W pad and on the PowKiddy RGB30; and which button is the right one for pad typing on each console, given that F1 and the menu say Share while the F12 list says Select]

While pad typing is on, the status line shows `PAD` and a small panel appears above it. [TO FILL: how to compose letters with the pad]

# Styling

## Paragraph styles

Press **F2** to go to the next style: Body, Title, Heading 1, Heading 2, Quote, Bullet list, Numbered list, then Body again. The toolbar shows the name of the style.

| Style | Look |
|---|---|
| Body | normal text, 72 letters per line |
| Title | large, bold, centred |
| Heading 1 | large, bold |
| Heading 2 | medium, bold |
| Quote | indented, italic |
| Bullet list | indented; one paragraph per item |
| Numbered list | indented; the numbers follow the paragraphs |

## Letters

Press **Ctrl+B**, **Ctrl+I** or **Ctrl+U** to switch bold, italic or underline.

- With a selection: the style is applied to the selected letters.
- With no selection: it applies to what you type next.

## Alignment

Press **F4** to go through left, centre, right and justify, or press **Ctrl+L**, **Ctrl+E**, **Ctrl+J**. A justified paragraph widens the words to the right margin; its last line stays on the left.

## Good to know

- Style and alignment belong to the whole paragraph. Bold, italic and underline belong to single letters.
- After a title or a heading, the next paragraph is Body. [TO FILL: what style a new paragraph gets after a quote or a list item]

![A document with a title, a heading, a list and a justified paragraph](shots/editor-write-typed.png)

# Saving, opening, exporting

## Where the files go

Everything goes in the folder `/docs` on the SD card. The folder is made by the first save. Take the card out and put it in your computer to find the files there.

## Saving

1. Press **Ctrl+S**.
2. The first time, a line asks `Name of the document (8 letters, in /docs):`. Type up to 8 letters or digits (`-` and `_` are fine), for example `LETTER`. The name is written in capitals and `.BMD` is added.
3. Press **Enter**. The status line says `saved /docs/LETTER.BMD`, and the `*` next to the name goes away.

The next **Ctrl+S** saves with the same name. **Ctrl+Shift+S** asks for a new name.

## Opening

1. Press **Ctrl+O**. If the document has changes, a box asks `This document has changes. Save them?` with Save, Discard, Cancel.
2. Pick a file from the list `Open a document (/docs)` with the up and down arrows. The list shows `.BMD`, `.MD` and `.TXT` files, with their size.
3. Press **Enter**.

A `.MD` or `.TXT` file opens as a new document with no name: **Ctrl+S** asks for a name and saves it as `.BMD`. Markdown marks are understood: `#` title, `##` and `###` headings, `>` quote, `-` bullets, `1.` numbers, `**bold**`, `*italic*`, `<u>underline</u>`.

## Exporting

1. Press **Esc**, go down to `Export...` and press **Enter**.
2. In the box `Export to /docs as`, pick `PDF`, `HTML`, `Markdown` or `Text`, and press **Enter**.
3. If the document has no name yet, bm Write asks for one. The status line says `exported /docs/LETTER.PDF` with the size in bytes.

| Format | File |
|---|---|
| PDF | `LETTER.PDF` |
| HTML | `LETTER.HTM` |
| Markdown | `LETTER.MD` |
| Text | `LETTER.TXT` |

An export is a copy to read or share. Save the `.BMD` too: it is the one you can edit with all the styles.

::: warn
An export with the same name replaces the older one in `/docs`.
:::

## Leaving

Choose `Exit` in the menu. If there are changes, a box asks you to save, discard or cancel. If you leave with Ctrl+Esc, the changes are asked about too; Ctrl+Esc a second time keeps them for next time.

# Tutorials

## Write and save your first document

::: try
Start at the console's menu, with the SD card in.
:::

1. Start **bm Write** from the menu. You see an empty page and the toolbar `bm Write` `Untitled`.
2. Press **F2** once. The toolbar shows `Title`. Type `A letter` and press **Enter**. The title is large and centred.
3. The new paragraph is Body. Type `This text has a ` then press **Ctrl+B**, type `bold`, press **Ctrl+B** again, and type ` word.` The word `bold` is thicker.
4. Press **Enter**. Press **F2** five times to reach `Bullet list`. Type `one`, **Enter**, `two`, **Enter**, `three`. The three lines have bullets.
5. Press **Ctrl+S**. Type `LETTER` and press **Enter**.
6. Look at the status line: it says `saved /docs/LETTER.BMD`, and the toolbar shows `LETTER.BMD` with no `*`.

If the console cannot write, the status line is red and says `cannot save LETTER.BMD:` with the reason. See the last chapter.

![A first document just typed](shots/editor-write-pages.png)

## Export it as PDF and read it on your computer

::: try
Start with `LETTER.BMD` open and saved, as in the tutorial above.
:::

1. Press **Esc**. The menu opens.
2. Go down to `Export...` and press **Enter**.
3. With `PDF` selected, press **Enter**.
4. Wait for `exported /docs/LETTER.PDF` on the status line.
5. Switch the console off and put the SD card in your computer.
6. Open the folder `docs`: `LETTER.PDF` is there. Open it with any PDF reader. The pages have the same lines as the screen, in Courier.

## Open it again

::: try
Start with the document closed, or after starting bm Write again.
:::

1. Press **Ctrl+O**. If bm Write asks to save changes, choose one.
2. The list `Open a document (/docs)` shows `LETTER.BMD` with its size, and the `.PDF` is not in the list.
3. Select `LETTER.BMD` with the arrows and press **Enter**.
4. The status line says `opened /docs/LETTER.BMD`. The text is as you left it.

You can also open `LETTER.MD`, if you exported it: it comes back with the same paragraphs.

# Tips, limits and troubleshooting

## Tips

- Save early with **Ctrl+S**, and often.
- Use **F1** when you forget a key.
- Use **Tab** to take the grey-blue word instead of typing it all. Change the language in the menu if the words are not yours.
- With a selection, **Esc** drops the selection first; press it again for the menu.

## Limits

- The name of a document is up to 8 letters or digits, `-` and `_` allowed, with no folders.
- A file written in `/docs` is at most 4 MiB.
- The body of a page holds 72 letters per line, in Courier.
- The text you did not save is kept for the next start only if it is under 24000 bytes.
- The title of the `.BMD` file keeps the first 60 letters of the first paragraph.
- [TO FILL: the largest number of pages or paragraphs a document can have on the console]
- [TO FILL: the letters that the font supports beyond the Latin ones with accents]

## Troubleshooting

| Problem | What to do |
|---|---|
| Saving, opening or exporting fails and the status line is red | Read the reason. If you said no to the question on documents, the calls fail: [TO FILL: how to change the answer; the answer is kept in `bm/config.txt`] |
| `no documents in /docs yet: Ctrl+S saves this one` | The folder is empty. Save a document first |
| `8 letters or digits, like LETTER` | The name has a space, or more than 8 letters (a final `.ext` is dropped: `LETTER.TXT` is taken as `LETTER`). Type a shorter one |
| `not a bm Write document` | The `.BMD` file is not in the bm Write format |
| The file you saved is not in the open list | Only `.BMD`, `.MD` and `.TXT` are listed. A PDF or an HTML cannot be opened |
| You closed the app without saving | Start bm Write again: if the changes were kept you see `the document with its changes is back (Ctrl+S saves it)` |
| The words suggested are in the wrong language | In the menu choose `Language`; `Word completion` switches them off |

::: note
Check the PDF on the console before you rely on it for anything important: [TO FILL: check the PDF on the Pi Zero W and open it on a PC, as the roadmap says]
:::
