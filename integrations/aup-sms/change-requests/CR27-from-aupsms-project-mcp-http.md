## CR-27 - Trasporto HTTP project-scoped per il MCP CBM

| Campo | Valore |
|---|---|
| Data | 2026-10-18 |
| Destinatario | `CodebaseMemory/codebase-memory-mcp-fork` |
| ID destinatario | da assegnare dal team owner |
| Classificazione | `new-capability` |
| Stato | `OPEN` |
| Fase ASMS interessata | `0660 — Trasporto HTTP del MCP CBM` |
| Run ASMS bloccata | binding MCP CBM project-scoped e collaudo Agent OS |
| Baseline owner verificata | `ec6427c38176efcd37b28682e82f2491afda94be` / binario consumer `0.12.0-asms.1` |

### Gap verificato

La baseline CBM espone il server MCP tramite stdio. La porta HTTP `9749`
appartiene a CBM Lens e non e' un endpoint MCP. ASMS richiede invece un
endpoint Streamable HTTP distinto per progetto, interrogabile da AUP Agent OS
con selezione esplicita della sorgente/indice.

Questa capability appartiene a CBM. Non deve essere simulata tramite
`aup_mcp_*`, proxy Lens, accesso diretto a SQLite o server MCP parallelo nel
modulo ASMS.

Evidenze sulla baseline owner:

- `CONTRIBUTING.md:58` classifica `src/mcp` come server JSON-RPC 2.0 su stdio
  con 17 tool;
- `src/main.c:5,1126,3592` dichiara il default stdio e passa esplicitamente
  `stdin, stdout` a `cbm_daemon_frontend_mcp_run`;
- `src/main.c:12,1148` e `README.md:165-171` assegnano la porta `9749` alla UI
  CBM Lens su loopback;
- `src/mcp/mcp.c:541-871,1113,20964-20966` contiene il catalogo nativo e il
  dispatcher `tools/list` / `tools/call`; `mcp.c:21113-21365` usa framing
  `Content-Length` sullo stream;
- nessuna occorrenza di `Streamable HTTP`, `Mcp-Session-Id` o
  `text/event-stream` e' presente sotto `src`.

Comandi riproducibili eseguiti sulla baseline dichiarata:

```powershell
rg -n "default.*MCP server on stdin/stdout|cbm_daemon_frontend_mcp_run\(.*stdin, stdout|Run MCP server on stdio" src/main.c
rg -n -e "--port=N" -e "127\.0\.0\.1:9749" -e "CBM Lens Beta is the browser interface" -- src/main.c README.md
rg -n "tools/list|tools/call|Content-Length:" src/mcp/mcp.c
rg -n -e "Streamable HTTP" -e "Mcp-Session-Id" -e "text/event-stream" -- src
```

Esito osservato: i primi tre comandi restituiscono le righe indicate; l'ultimo
termina con exit code `1` (`NO_MATCHES`). Il trasporto Streamable HTTP e'
quindi genuinamente assente, mentre dispatcher e catalogo tool sono riusabili.

### Struttura richiesta

L'estensione deve restare isolata per facilitare il recepimento delle future
versioni upstream, sotto una directory dedicata analoga a:

```text
integrations/aup-sms/mcp-http/
  README.md
  src/
  tests/
```

Il core CBM riceve soltanto seam additivi e tipizzati. Il trasporto stdio
esistente resta disponibile e mantiene il comportamento precedente.

### Contratto richiesto

1. trasporto MCP Streamable HTTP conforme alla versione protocollo adottata;
2. endpoint distinto e health invocabile per ogni progetto ASMS;
3. autenticazione obbligatoria fail-closed con credenziale revocabile e
   project-scoped; nessun `none` in produzione;
4. input `source_id` obbligatorio nei tool codice; discovery di applicazioni e
   sorgenti resta nel distinto MCP AUP-SMS e non viene duplicata in CBM;
   nessun path fisico, database SQLite, tenant o progetto viene accettato dal
   modello;
5. risoluzione server-side del binding `source_id -> repository_index_id`
   `READY`, senza scelta implicita dell'ultimo indice;
6. supporto multi-source nello stesso endpoint logico di progetto;
7. lifecycle esterno start/stop/restart e health coerente con il processo CBM;
8. errori originali, cause e correlazione preservati nel risultato MCP;
9. limiti, timeout, profondita' e dimensione delle risposte configurabili;
10. nessuna dipendenza dalla porta Lens o dalla sessione UI.
11. catalogo tool nativo CBM riusato senza replica in `aup_mcp_*`, con
    allowlist iniziale read-only per i Team Agent OS;
12. audit, metriche, correlation id e diagnostica del trasporto nativo
    consultabili separatamente dai log `aup_mcp_call_*` del server AUP-SMS.

### Control plane ASMS -> CBM richiesto

ASMS resta source of truth di tenant, progetto, sorgente, stato `READY` e
indice attivo. CBM non legge il database ASMS. Un'API amministrativa versionata
e distinta dal data plane MCP materializza nel runtime CBM soltanto
un'attestazione firmata:

```text
PUT    /integrations/aup-sms/v1/projects/{project_scope}/sources/{source_id}
DELETE /integrations/aup-sms/v1/projects/{project_scope}/sources/{source_id}
GET    /integrations/aup-sms/v1/projects/{project_scope}/health
```

Il chiamante usa una credenziale control-plane separata dai token Agent OS.
Tenant e `project_scope` attestati dalla credenziale devono coincidere con il
path; non sono accettati dal modello. Il `PUT` contiene almeno
`binding_version`, `repository_index_id`, identificatore interno CBM,
generazione, SHA-256 dell'artifact verificato, `activated_at` ed `expires_at`.
Identificatore interno, locator e checksum non vengono restituiti dai tool MCP.

`Idempotency-Key` rende sicura la ripetizione; `If-Match` sulla revisione
impone compare-and-swap. Un solo binding per `source_id` puo' essere attivo e
la sostituzione e' atomica. CBM verifica esistenza locale, generazione e
checksum prima dell'attivazione. Binding scaduto, revocato, non verificabile o
con revisione concorrente fallisce prima dell'accesso al grafo. Il `DELETE` e'
idempotente e revoca immediatamente il data plane; un nuovo `PUT` non riattiva
implicitamente un binding precedente.

Gli stati minimi sono `INSTALLING`, `VERIFYING`, `ACTIVE`, `DEGRADED`,
`FAILED`, `REVOKED`, `EXPIRED`. Ogni risposta e failure usa un envelope
versionato con tipo, codice, messaggio, dettagli, request/correlation id e
cause. Il rollback disattiva la nuova revisione; non seleziona un altro indice
e non modifica l'artifact.

### Sicurezza dell'audit nativo

Il trasporto CBM registra per default soltanto metadati: progetto attestato,
`source_id`, tool, esito, durata, dimensione, correlation id e diagnostico
redatto. Query, risultati, sorgente, token, header e credenziali non vengono
persistiti. Redazione avviene prima della scrittura e non e' disattivabile in
produzione per segreti, credenziali, PII e sorgente classificato.

Retention, dimensione massima per record e quota per progetto sono
configurabili con default sicuro. Lettura e statistiche richiedono scope
tenant/progetto verificato e non accettano scope autorevole dalla request. In
produzione l'audit e' `required`: indisponibilita' prima dell'esecuzione
rifiuta la chiamata; un guasto successivo resta un failure osservabile. Se
falliscono sia tool sia audit, l'errore del tool resta primario e quello audit
e' conservato come causa secondaria, senza falso successo.

### Criteri di accettazione owner

1. client MCP HTTP standard esegue initialize, tools/list e tools/call;
2. due progetti non condividono endpoint logico, credenziali, source o indici;
3. due source dello stesso progetto sono interrogabili passando `source_id` e
   producono risultati dall'indice esatto;
4. source cross-project, indice non READY, binding assente e credenziale
   revocata falliscono prima dell'accesso al grafo;
5. stop/restart e health sono provati su Windows e Ubuntu;
6. crash, timeout e failure CBM conservano diagnostica e correlation id;
7. transport stdio e suite upstream restano verdi senza l'estensione attiva;
8. test protocollo, concorrenza, isolamento e mancato uso della porta Lens;
9. documentazione di deployment, configurazione, secret rotation e rollback;
10. build/binario qualificato con versione e SHA-256 pubblicati.
11. registrazione, rinnovo, CAS concorrente, scadenza e revoca del binding
    control-plane sono coperti senza accesso di CBM al database ASMS;
12. raw query, risultati, source, token e header non compaiono nell'audit;
    retention, quota, redazione e autorizzazione read sono provate;
13. failure di audit singola e doppia conserva esito, errore primario, causa
    secondaria e correlation id senza conversione in successo;
14. il client/UI di health e audit e' versionato separatamente dal pannello
    `aup_mcp_call_*` richiesto dalla CR-26.

### Condizione di adozione ASMS

ASMS non implementa un trasporto CBM alternativo. La fase 0660 resta bloccata
fino a `DELIVERED`; dopo l'aggiornamento del pin/binario, ASMS verifica la CR
come `VERIFIED`, registra l'endpoint del progetto e solo allora crea il binding
Agent OS `route_mode=direct`.

### Rettifica append-only dell'ambito bloccato — 2026-10-18

Il riferimento storico a «binding MCP CBM project-scoped» e la frase «solo
allora crea il binding» indicano l'attivazione operativa del binding, non la sua
modellazione o persistenza. La CR-27 blocca disponibilita' del trasporto,
attivazione e collaudo live/end-to-end del binding CBM. Non blocca modello,
migration, API, UI, provisioning Agent OS o registrazione del binding `direct`
in stato inattivo contro contratto e fixture. Il binding diventa attivabile
soltanto dopo `DELIVERED`, aggiornamento del pin e verifica `VERIFIED`.

### Rettifica append-only su telemetria e UI — 2026-10-18

I riferimenti precedenti a telemetria «consultabile», lettura/statistiche e
client/UI health-audit sono rinviati con 0659 e CR-26 e non fanno parte della
consegna corrente. Restano obbligatori soltanto audit tecnico interno redatto,
retention e failure contract, metriche operative interne, correlation id,
diagnostica originale e health machine-readable. Non sono richiesti ora API
utente di consultazione, statistiche, pannelli, link ai trace o componenti UI;
il criterio owner 14 sopra e' quindi differito e non blocca 0660.



