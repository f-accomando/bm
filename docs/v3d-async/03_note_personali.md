# Note personali - perché queste cose

Situazione attuale di bm:
- ARM fa tutto il setup vertici (~2-2.8 µs/tri).
- v3d_run è sincrono: il tempo GPU si somma al tempo ARM.
- Fill rate già ottimo (811 Mpixel/s).
- Limite pratico ~6-7k triangoli a 60 fps quando c'è solo 3D; molto meno quando c'è Lua/fisica.

Cosa cambia con async + triple buffer:
- Il tempo di binning+rendering (2-7 ms) non blocca più l'ARM.
- In un gioco reale quel tempo si sovrappone a simulazione e logica.
- Risultato atteso: più margine per triangoli o per altra logica a 60 fps.
- Non aumenta il fill rate né il picco teorico di triangoli della GPU.
- Aumenta il throughput effettivo del sistema.

Perché non altro prima:
- Vertex shader è più complesso e i tentativi precedenti non sono andati.
- Impostor aiutano ma sono una tecnica di contenuto, non di pipeline.
- Async è il cambiamento con miglior rapporto sforzo/beneficio oggi.

Ordine sensato:
1. Async + interrupt (o polling).
2. Doppio buffering.
3. Triple se serve.
4. Poi valutare vertex shader o impostor.

Rischi:
- Coerenza cache.
- OUTOMEM del binner.
- Latenza di 1-2 frame (accettabile).
