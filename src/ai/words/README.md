# Word completion texts (M30)

Italian version: [README-IT.md](README-IT.md).

The texts from which `scripts/mkwords.py` counts the words for completion
(`src/ai/predict.lua`, guide in `docs/PREDICT.md`) and from which `scripts/syllables.py`
counts the syllables. They are **written specifically for bm** (2026-10-01), in modern
Italian: no text taken from books, websites or collections under other people's licences.

| File | What |
|---|---|
| `it_narrativa*.txt` | short stories with dialogue |
| `it_quotidiano*.txt` | messages, emails, invitations, phone calls |
| `it_giochi*.txt` | video game dialogue and texts, tutorials, reviews |
| `it_tecnica*.txt` | programming and technology explained to beginners |
| `it_informativi*.txt` | news, recipes, travel, school, sport |
| `it_lettere*.txt` | diary, letters, postcards |
| `it_lessico.txt` | ~6 500 common forms (one per line), for the words the texts lack |
| `en_testi*.txt` | English: code comments, games, messages |

The file name before `_2`, `_3` is the group: `make predict-bench` removes one group at a
time from the dictionary and measures completion on its texts. The benchmark's test texts
(`tests/predict/texts.lua`) are not here. To improve prediction, add texts (UTF-8, real
accents; the code converts them to CP437), then `make`.
