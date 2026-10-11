# Implementazioni dettagliate

## File: v3d_async.c / v3d_async.h

**A cosa serve**: Submit asincrono dei job V3D.

**Cosa fare**:
- Definire `v3d_job_t` con start/end delle control list e stato.
- `v3d_async_init()`: abilita interrupt `FLDONE | FRDONE | OUTOMEM`.
- `v3d_submit(job)`: scrive `CT0CA` + `CT0EA` e ritorna.
- `v3d_irq_handler()`: su `FLDONE` avvia rendering (`CT1EA`), su `FRDONE` marca done.
- `v3d_wait()` / `v3d_is_done()` per sincronizzazione.

**Esempio minimo**:
```c
v3d_job_t job = { .bin_start = ..., .bin_end = ..., .render_start = ..., .render_end = ... };
v3d_submit(&job);
// ARM continua
do_other_work();
v3d_wait(&job);
```

## File: frame_manager.c / frame_manager.h

**A cosa serve**: Triple buffering dei buffer di vertici e control list.

**Cosa fare**:
- Array di 3 `frame_slot_t`, ognuno con i propri buffer.
- `frame_acquire()`: restituisce uno slot libero (aspetta se necessario).
- `frame_submit()`: cache clean + `v3d_submit`.
- Nell'IRQ: marca lo slot completato.

**Esempio**:
```c
frame_t *f = frame_acquire();
prepare_vertices(f->vertices);
build_cls(f);
frame_submit(f);
// prepara già il prossimo se vuoi
```

## File: modifiche a src/v3d.c

**A cosa serve**: Sostituire la chiamata sincrona esistente.

**Cosa fare**:
- Rinominare o wrappare l'attuale `v3d_run` in modo che usi `v3d_submit`.
- Spostare la costruzione delle control list in modo che scriva nei buffer del frame slot.
- Aggiungere i punti di cache clean prima del submit.

## File: interrupt handler (kernel)

**A cosa serve**: Chiamare `v3d_irq_handler` quando arriva l'IRQ della V3D.

**Cosa fare**:
- Individuare dove vengono gestiti gli interrupt periferici.
- Aggiungere il controllo su `V3D_INTCTL` e la chiamata all'handler.
- In alternativa (più semplice all'inizio): polling periodico di `V3D_INTCTL` nel main loop.

## File: gestione OUTOMEM

**A cosa serve**: Evitare che il binner si blocchi con tante primitive.

**Cosa fare**:
- Allocare un pool iniziale (256-512 KB) e impostare `V3D_BPCA` / `V3D_BPCS`.
- Nell'handler `OUTOMEM`: allocare altro blocco, scrivere `V3D_BPOA` / `V3D_BPOS`.

## Note comuni a tutti i file

- Allineare i buffer a 32 byte.
- Non riusare un buffer finché il job corrispondente non è `DONE`.
- Testare prima con doppio buffering, poi passare a 3.
