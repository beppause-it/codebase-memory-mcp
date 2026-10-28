# CR-37 — Consegna CBM e confine Agent OS

Data: 2026-10-28. Nessun commit, push o deployment implicito.

Il produttore CBM aggiunge il diagnostico `aup.exception` 1.0 nelle posizioni
canoniche: `error.data.diagnostic`, `result.structuredContent.diagnostic` per
tool falliti, e `diagnostic` per errori di controllo. Status, codice, messaggio,
contenuto e cause legacy restano disponibili. I limiti di dettaglio, messaggio
e numero di cause sono dichiarati; nessuno stack C o riferimento completo viene
inventato. La diagnostica della richiesta viene azzerata prima del parsing HTTP,
evitando di ereditare cause di richieste precedenti.

La richiesta originale CR-37 resta invariata. La CR Agent OS è stata depositata
prima della ripresa della build, in
`C:/laragon/www/api2mcp-api/docs/change-requests/CR24-from-cbm-mcp-http-error-body-preservation.md`.

## Precisazione della risposta owner alla CR24

L'owner ha verificato che l'adapter completo Agent OS possiede già un hook che
legge il body HTTP d'errore. Il difetto riprodotto con tale adapter è nel fallback
legacy del codec, che perde i campi nativi dopo aver ricevuto il body. Il probe
SDK semplice conservato qui usa SDK e codec reali ma NON PinnedMCPTools né
McpPinnedHttpTransport; non dimostra il comportamento dell'adapter completo.
La sua documentazione è stata corretta per esplicitare questo confine.

Il budget nuovo di cattura 256 KiB / 2 secondi è responsabilità del consumer
Agent OS; non richiede modifiche al protocollo o al produttore CBM. I test owner
devono usare l'adapter completo e il percorso ManagedAgentExecution/ASGI.
Il team CBM non modifica il runtime Agent OS. La disponibilità della sorgente
resta distinta: SOURCE_NOT_ACTIVE continua a essere un errore reale.

## Artefatto

Build ufficiale pulita con UI:

```text
scripts/build.sh --with-ui --version 0.12.0-asms.cr37.1 BUILD_DIR=build/cr37-native CC=clang CXX=clang++
```

Binario: `build/cr37-native/codebase-memory-mcp.exe`.
Versione: `0.12.0-asms.cr37.1`. Dimensione: 335377408 byte.
SHA-256: `ca5f7411624945cf79c1addcc1c0400c1136d9c5fc4e21854e7df11789d327e9`.

Log: `build/cr37-build.log`, `build/cr37-serializer-tests.log`,
`build/cr37-http-tests.log`, `build/cr37-service.log`, `build/cr37-ui-smoke.log`.
Il binario consumer precedente e il file .env ASMS non sono stati sostituiti.
