# CR-38 — Stop amministrativo tra build

29 ottobre 2026. Implementazione CBM; qualificazione Windows, limiti sotto.

## Decisione del richiedente

L'utente ha richiesto esplicitamente la stessa primitiva, senza opzione nuova.
`daemon stop [--json]` permette quindi lo stop cooperativo cross-build per
default. Questa modifica del comportamento in caso di mismatch è intenzionale
e sostituisce la proposta opzionale della CR originale. Nessuna modifica a
start, HELLO, attach o query: le sessioni operative miste restano rifiutate.

## Contratto

Riutilizzati STATUS/STOP e layout nativi già presenti in CR36, senza nuovo ABI,
autenticazione o fallback di produzione a vecchi eseguibili. Il daemon verifica
il vero fingerprint del processo richiedente; non gli viene attribuita
l'identità della build attiva. Il client verifica che il peer della connessione
STOP corrisponda al PID e al fingerprint mappato rilevati da STATUS. Un cambio
di identità non produce uno stop verso un altro daemon.

La verifica busy e la transizione stopping restano atomiche nel daemon sotto
il mutex esistente, senza kill o drain forzato. La risposta `stopping` è una
richiesta accettata, NON prova di processo terminato: il consumer deve attendere
la scomparsa del daemon nel medesimo contesto e verificare il nuovo avvio.

JSON mantiene status, accepted, pid, committed_clients e client_pids, con campi
aggiuntivi active_version, active_build, requested_version e requested_build.
Esiti: absent `not_running`/exit 0; accepted o già stopping `stopping`/exit 0;
busy/exit 6; controllo non disponibile o identità cambiata/exit 4, accepted=false.
Uno stop rifiutato per sola differenza di build non restituisce più exit 5.
Gli errori locali di controllo sono espliciti anche su stderr. Nessuno stack C
viene inventato e nessun envelope HTTP viene imposto al protocollo IPC nativo.

## Prova ripetibile

```text
python tests/test_daemon_cross_version_stop.py NEW_BINARY OLD_BINARY
```

Lo script usa HOME, USERPROFILE, cache e runtime privati; testa absent,
cross-build, start ancora rifiutato, busy con client reale, stop dopo chiusura
del client, idempotenza e avvio della nuova build. Nessun kill forzato.
Il vecchio binario è una baseline esplicita di test, non un fallback consumer.

Nessuna modifica al runtime ASMS, .env o daemon reali dell'utente.
La sola implementazione non costituisce VERIFIED dal team ASMS.

## Evidenze intermedie — 29 ottobre 2026

Build nativa standard `0.12.0-asms.cr38.1` completata tramite
`scripts/build.sh`, log `build/cr38-build.log`. La variante con UI viene
prodotta dal target `cbm-with-ui` con identici sorgenti/versione e gli oggetti
engine della build pulita, log `build/cr38-ui-build.log`.

Prova reale Windows contro `build/cr36-final/codebase-memory-mcp.exe`:
PASS, exit 0, log `build/cr38-cross-version.log`. Verificati busy con client
reale, stop dopo chiusura client, idempotenza, vecchio daemon ritirato e nuova
build avviabile. Baseline CR36 SHA256
`f8924842dec0189c79671916ec666f7d0e243e14d1d0a1142ac6a4a212e154cf`.

Compilabilità dei file modificati verificata anche con GCC Linux/WSL,
`-Wall -Wextra -Werror -fsyntax-only`, exit 0, log
`build/cr38-linux-syntax.log`. Questa verifica NON è un collaudo end-to-end
cross-version su Linux. Restano da qualificare completamente su tale OS
il vecchio daemon, ABI incompatibile, endpoint estraneo e race PID.

## Binario con UI

`build/cr38-native/codebase-memory-mcp.exe`, versione `0.12.0-asms.cr38.1`.
Dimensione 335378432 byte. SHA256:
`f8bf50857cbdd7c7a2621d9fc20de887f3e378d79d14a4e78fafab5823b6e34a`.

Il test cross-version contro il vero CR36 è passato anche sull'EXE finale con
UI: `build/cr38-ui-cross-version.log`. UI HTML e asset HTTP effettivamente
serviti, arresto processo e daemon privato: PASS, `build/cr38-ui-smoke.log`.
Il file .env e i binari configurati nel deployment ASMS non sono sostituiti.

PASS anche per stessa versione `0.12.0-asms.cr38.1` ma build diverse: finale
con UI contro variante standard (fingerprint
`afa69f90882d609083ca9033c531a515531e825ca707b2103c692d2455591eef`),
log `build/cr38-same-version.log`. Lo stesso script verifica anche stop e
riavvio con la stessa build, assenza e client attivi.

CR39, depositata durante questa build, è distinta: schema tools/list nativo e
ceiling HTTP non sono coerenti. Il difetto è confermato dall'ispezione di
tool_schema/integer_ceiling_valid, ma NON è corretto nel binario CR38.
