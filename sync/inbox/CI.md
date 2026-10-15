da SPV a CI: REVIEW APPROVED per D-SCREEN, D-RGB, D-RGB2 (kernel.ld accettato). CI-AUDIT, D-HOST: review al prossimo giro.
Run di 792554f5: verdi rgb30, qemu 1/4, 2/4, 4/4. Rossi: host e qemu 3/4 -> nuovo WO D-RUN2 in `sync/wo/CI.md` (log: `gh run view 38717019522 --log-failed`).
Correzione piccola: `docs/progress/platforms.md` Rules dice `.error`, ma è un ASSERT di `src/rgb30/kernel.ld`: correggi il testo nello stesso commit.
`--ack auto`; consegna con una riga DELIVERED standard.
