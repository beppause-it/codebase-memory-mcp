# CR-38 — Arresto amministrativo CBM tra versioni differenti

## 0830 — Richiesta e ambito

Data: 29 ottobre 2026. Richiedente: AUP-SMS. Owner: CBM,
CodebaseMemory/codebase-memory-mcp-fork. Stato: PROPOSED.
Classificazione: contract-change / hardening.
Richiesta umana esplicita: il cambio di versione non deve impedire di arrestare
il daemon precedente. Sospesa la precedente proposta di controllo UI ASMS;
prima deve essere disponibile la primitiva nativa sicura richiesta qui.
Riferimenti: analisi0820, CR-36 (shadow), CR-37 (diagnostici distribuiti).
CR-38 non sostituisce queste richieste e non consente esecuzioni miste.

Blocca il controllo completo del daemon dalla pagina AUP2 Processi asincroni
quando il supervisore e fallito ma resta attivo un daemon di build precedente.
ASMS non deve selezionare vecchi exe, usare taskkill, falsificare l'identita
del client o implementare un protocollo di arresto parallelo.

## Evidenza verificata

Caso utente: supervisore Lens FAILED, daemonPID41580 ancora attivo.
Attivo0.12.0-asms.cr36.1, build
f8924842dec0189c79671916ec666f7d0e243e14d1d0a1142ac6a4a212e154cf;
richiesto0.12.0-asms.cr37.1, build
ca5f7411624945cf79c1addcc1c0400c1136d9c5fc4e21854e7df11789d327e9.
Il messaggio del processo segnala conflitto di versione; successiva lettura
CIM ha confermato PID41580 con percorso build/cr36-final/codebase-memory-mcp.exe.
Questa e evidenza storica, non una nuova verifica di liveness al deposito.

Il comando manuale dalla HOME PowerShell predefinita ha restituito not_running.
ASMS usa invece CodebaseMemoryProcess.processEnvironment (HOME/USERPROFILE
dedicati) e NativeLensDaemonControl.environment (CBM_CACHE_DIR e
CBM_RUNTIME_DIR/lens). Il mancato ritrovamento in un altro contesto non prova
che il daemon sia fermo; non cercare/fermare genericamente tutti i processi CBM.

Codice owner ispezionato in sola lettura:
- src/main.c:2560, main_daemon_ctl_build_conflict confronta fingerprint;
- src/main.c:2566, status JSON espone pid, versione/build, client e stopping;
- src/main.c:2711-2718, stop ritorna build_conflict/accepted=false/exit5 prima
  di inviare la richiesta cooperativa quando il fingerprint e diverso;
- src/main.c:2721 e src/daemon/runtime.c:3074, richiesta stop nativa esistente;
- src/main.c:2734-2745, busy/exit6 protegge i client ancora collegati.

Comando riproducibile dalla root CBM:
```powershell
rg -n 'main_daemon_ctl_build_conflict|main_daemon_ctl_print_status_json|build_conflict|stop_result.busy' src/main.c
rg -n 'cbm_daemon_runtime_request_stop|runtime_control_request_send' src/daemon/runtime.c
```
Esito osservato: rifiuto stop per mismatch build presente, prima della primitiva
cooperativa. Non e solo il controllo di qualificazione configurabile in ASMS.

## Riproduzione richiesta in ambiente isolato

1. Preparare un contesto di test con HOME, cache e runtime privati, usando le
   convenzioni native per ciascun OS. Mai il daemon/sessioni reali dell'utente.
2. Avviare daemon A permanente con binario CR36 e chiudere il client di avvio.
3. Dal binario B CR37, nello stesso contesto, eseguire daemon status --json:
   rilevare A, con identita e PID corretti, non not_running.
4. Richiedere esplicitamente l'arresto cross-version con la capability nuova:
   oggi daemon stop --json si ferma a build_conflict/exit5.
5. Senza client attivi, B deve poter chiedere ad A l'arresto cooperativo sicuro;
   distinguere accepted/stopping da arresto effettivamente verificato.
6. Verificare terminazione di A e rilascio dell'endpoint, poi avviare B senza
   conflitti residui. Nessun kill forzato o modifica a database/grafi/config.
7. Ripetere con un client attivo: busy esplicito, nessuna interruzione forzata;
   chiudere il client, ripetere stop e verificare il completamento.

Baseline da collaudare esplicitamente: daemon CR36 gia distribuito e client
nuovo con la capability. Non limitare il test a due binari entrambi ricompilati
con la correzione. Se il vecchio protocollo rende il caso impossibile senza
indebolire controlli, dichiarare il limite e consegnare una migrazione sicura:
non dichiarare risolto il caso reale o introdurre fallback nascosti.

## Contratto e accettazione

1. Separare ammissione di client operativi da controllo amministrativo di stop.
   Start, attach, query e sessioni possono continuare a rifiutare build diverse;
   il mero mismatch versione/build non deve rendere non arrestabile il daemon.
2. Riutilizzare IPC/controllo nativo e autorizzazione locale esistenti. Verificare
   identita OS/account/endpoint e contesto atteso, risposta del daemon e PID
   effettivo; nessuna nuova autenticazione, registro ownership fittizio o
   fiducia in un PID fornito liberamente dall'utente.
3. Un fingerprint/versione differente non equivale a protocollo amministrativo
   incompatibile. La compatibilita del control plane va verificata realmente;
   se non compatibile, errore specifico con dettagli originali, non genericamente
   not_running. Non aggirare autenticazione o controlli IPC esistenti.
4. Esporre capability documentata e invocabile da ASMS con output JSON stabile,
   semplice e multipiattaforma. Preferire estensione opzionale/versionata del
   comando esistente (esempio non vincolante --allow-version-mismatch), con
   omissione che conserva comportamento/default pubblicati. Non introdurre
   cambi breaking silenziosi: owner dichiara il contratto e l'adozione consumer.
   ASMS potra invocare esplicitamente la nuova modalita dalla UI autorizzata.
5. Busy/client attivi non sono consenso al kill: mantenere rifiuto o drain
   cooperativo esplicito gia supportato, con deadline e stato osservabile.
   Nessun arresto automatico di sessioni esterne, SIGKILL/taskkill o processo
   globale trovato per nome. Non cancellare lock/file per simulare un arresto.
6. Idempotenza: daemon assente, gia stopping e stop ripetuto devono avere esiti
   distinti e documentati; nessun successo STOPPED finche non verificato.
   Gestire race di cambio PID/build e nuovi client con controlli nativi.
7. Conservare codice, messaggio, causa, exit code, stdout/stderr diagnostici,
   versione/build attive e richieste, PID, client e correlazioni disponibili.
   Riutilizzare protocollo distribuito di CR-37/aup.exception1.0 quando previsto;
   non rinominare o nascondere eccezioni, non fabbricare stack C inesistenti.
   Redigere solo segreti/PII, trace complete nei canali autorizzati.

## Test e consegna

- stessa build, versione diversa e stessa versione con build diversa;
- nuovo client/vecchio daemon CR36 realmente distribuito, non solo fixture;
- busy, stopping, absent, not_responding e control ABI non supportata;
- utente/endpoint/contesto estranei, PID riutilizzato e race client/daemon;
- retry idempotente e verifica finale: endpoint libero e nuova build avviabile;
- Windows e Linux, output CLI/JSON retrocompatibile, nessun kill forzato;
- propagazione diagnostica completa e nessun fallback a un vecchio exe scelto
  senza un contratto di migrazione verificabile.

Consegna con baseline sorgente, binario/versione/SHA256, contratto CLI/control,
comandi e test con risultati reali, limiti e percorso di upgrade. Stato DELIVERED
solo con evidenze owner; VERIFIED dopo collaudo ASMS sulla baseline fissata.

## Ambito del deposito

Solo documentazione nel registro ASMS e copia nella cartella CR CBM. Nessun
codice, migration, UI, auth, processo, binding o credenziale modificato per
questa richiesta; nessun arresto reale eseguito. Nessun commit/push.
Review documentale: PENDING_ARCHITECT_REVIEW.

### Esito review — 29 ottobre 2026

Release Gate documentale indipendente architect_0820 APPROVED, nessun finding.
Consentita la sola copia documentale nel progetto CBM; stato tecnico PROPOSED.
UI ASMS sospesa. Nessun arresto o superamento del conflitto dichiarato.
