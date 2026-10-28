# Agent OS CR24 - Conservare il body degli errori MCP prima della chiusura dello stream

Data: 2026-10-28. Richiedente: CBM / AUP-SMS.
Owner destinatario: AUP Agent OS, `C:/laragon/www/api2mcp-api`.
Classificazione: verified-bug / hardening. Stato: PROPOSED.
Collegata a CR23 Agent OS e CR-37 CBM; non le sostituisce o chiude.
La numerazione CR24 e' del destinatario Agent OS, distinta dalla CR-24 Lens.

## Richiesta umana e confine

L'utente richiede che nessuna eccezione venga nascosta e che venga mostrato
l'errore reale attraverso il protocollo distribuito esistente. Ha autorizzato
qui soltanto il codice CBM e l'handoff al team Agent OS. Questa richiesta
documentale non modifica runtime, dipendenze, configurazione o autenticazione
Agent OS e non autorizza il team CBM a sostituirne localmente l'adapter.

La CR viene depositata prima di riprendere compilazione e nuova release
dell'eseguibile CBM. La build CBM in corso e' stata interrotta a tale scopo.
Non viene dichiarata disponibile una nuova release CBM per questa richiesta.

## Difetto riprodotto e baseline

Il CBM nativo restituisce gia' HTTP 400 con JSON-RPC error.code=-32000 e:

```text
error.data.code = SOURCE_NOT_ACTIVE
error.data.message = Exact source binding is absent, revoked, expired or unverifiable
error.data.request_id / correlation_id = identificatori nativi presenti
```

SOURCE_NOT_ACTIVE e' un rifiuto reale: il binding e' assente, revocato,
scaduto o non verificabile. Deve restare un fallimento. Questa CR non attiva
binding e non corregge la disponibilita' della sorgente.

Baseline del riproduttore reale:

- CBM sorgente `a7211dbb33dfc5d8fca77d03507d1135581ae8e1`;
- binario `build/cr36-final/codebase-memory-mcp.exe`, `0.12.0-asms.cr36.1`;
- SHA-256 binario `f8924842dec0189c79671916ec666f7d0e243e14d1d0a1142ac6a4a212e154cf`;
- Agent OS HEAD locale `24880dfce2a5662117e00de7116e3b76dfa37660`, working tree
  pulito prima del deposito di questa CR;
- Python 3.12.11, SDK MCP Python 1.28.1, HTTPX 0.28.1, letti dal virtualenv
  `api2mcp-api/runtime/.venv` realmente usato nella prova.

Usati il vero processo CBM, il vero SDK MCP ClientSession e il vero
DistributedExceptionCodec Agent OS. Solo configurazione e credenziali sono
fixture effimere private: nessun accesso al deployment tenant1/progetto3,
nessuna revoca, provisioning, run LLM o credenziale di produzione.

Esito effettivo del probe, exit 0:

```json
{"status":400,"response_body_available":false,"response_closed":true,"primary_type":"builtins.ExceptionGroup","completeness":{"complete":false,"limitations":["upstream_diagnostic_unavailable"],"diagnostic_reference":null},"contains_native_code":false}
```

Il body non e' piu' leggibile quando il diagnostico viene catturato: il
codice CBM non compare nel risultato del codec. La prova attribuisce questa
perdita al percorso SDK/adapter/codec testato, non al produttore CBM.
Non dimostra la causa del binding inattivo o l'identita' del body di una
precedente run Team storica.

## Punti verificati nei sorgenti destinatari

1. `runtime/.venv/Lib/site-packages/mcp/client/streamable_http.py:358`:
   `_handle_post_request` chiama `response.raise_for_status()` dentro
   `client.stream(...)`, prima della lettura del body d'errore.
2. `runtime/aupaos_runtime/mcp_pinned_http_transport.py:38`: il trasporto
   restituisce intenzionalmente una Response in streaming. Conservare questo
   comportamento per risposte normali e SSE.
3. `runtime/aupaos_runtime/diagnostics/distributed_exception_codec.py:91-93`:
   la lettura `current.response.json()` incontra ResponseNotRead e prosegue
   senza il diagnostico remoto.
4. Stesso codec, `node()` intorno a riga 121: per HTTPStatusError viene
   conservato upstream_status; il fallback attuale non conserva il body
   legacy autorizzato, content type e correlazioni remote come contesto.

Non modificare site-packages come soluzione: riusare il confine HTTP/adattatore
Agent OS esistente. Nessun wrapper per singolo tool o fork dell'SDK.

## Contratto da riusare

Riferimenti canonici destinatari:

- `docs/agent-os/adr/0034-distributed-exception-protocol.md`;
- `contracts/distributed-exception/1.0/schema.json`;
- `docs/change-requests/CR23-from-aupsms-team-run-root-cause-and-distributed-exceptions.md`.

Riusare `aup.exception` 1.0 e il codec attuale. JSON-RPC conserva il codice
di protocollo separato dal codice nativo. La collocazione prevista da ADR0034
e' `error.data.diagnostic` per RPC fallite e
`result.structuredContent.diagnostic` con `isError=true` per tool falliti.
Il produttore CBM sta adeguando la propria emissione additiva nella CR-37;
nuova baseline e hash verranno consegnati separatamente, dopo il collaudo.
Non assumere gia' installato o rilasciato quel produttore.

## Correzione richiesta al team Agent OS

- Conservare il body delle risposte HTTP d'errore prima che il relativo stream
  venga chiuso o l'eccezione del TaskGroup raggiunga il codec. Applicare un
  limite esplicito gia' previsto dal trasporto, senza leggere indefinitamente
  risposte SSE o trasformare le risposte riuscite in buffering completo.
- Se presente un diagnostico canonico valido, mantenere diagnostic_id, origin,
  primary, secondary e hop remoti; aggiungere soltanto il contesto/stack locale.
- Se il diagnostico manca o e' invalido, conservare il body legacy autorizzato,
  status, content type e ID/correlazioni realmente disponibili. Dichiarare
  incompletezza, non limitarsi al solo HTTPStatusError o a "HTTP 400".
- Conservare origine, tipo, codice e messaggio nativi, tutte le cause disponibili
  e gli errori secondari. Il codice locale AUPAOS_MCP_TRANSPORT_FAILED rimane
  un wrapper di confine: non sostituisce il codice CBM originale.
- Non inventare stack C, classi remote, cause o riferimenti a trace complete.
  Stack unavailable del produttore e body non ricevuto sono condizioni distinte.
- Esporre messaggio/codice reali nelle viste diagnostiche gia' autorizzate.
  Non creare una nuova piattaforma log, autenticazione o pagina parallela.
- Non cambiare binding, reti, tenant routing, snapshot, dependency pin,
  provisioning o lifecycle sulla sola base di questo riproduttore.

## Test e accettazione

1. Ripetere la prova con SDK 1.28.1 e adapter reale: SOURCE_NOT_ACTIVE resta
   fallimento, ma body e codice/messaggio nativi sono conservati nel diagnostico.
2. Con il produttore CBM CR-37 qualificato, validare contro lo schema canonico
   e confrontare origin, primary.code/message, request/correlation ID e cause
   ai confini CBM -> relay ASMS -> SDK -> codec Agent OS -> UX autorizzata.
3. Verificare errore artefatto con verification_stage e dettagli OS/SQLite;
   errore worker annidato; errore secondario di audit/cleanup che non sostituisce
   il primario; ExceptionGroup e vera cancellazione esterna distinti.
4. Verificare risposta legacy, testo/non JSON, diagnostico invalido, body
   troncato e trasporto senza risposta. Nessuna eccezione nascosta o inventata;
   limiti e disponibilita' della trace restano espliciti.
5. Regressioni SSE in streaming, connessioni/host/SNI autorizzati, header,
   tenant isolation, redazione segreti e policy di stack. Nessuna chiamata LLM
   o modifica a binding reali necessaria per questi test.
6. Consegnare matrice per hop dei campi conservati/perduti, baseline sorgente,
   versioni/hash, comandi/esiti e limiti. Non dichiarare risolta la run Team
   storica o attivata la sorgente soltanto perche' il diagnostico e' migliorato.

## Riproduttore e consegna

Il riproduttore conservato nel repository CBM e':
`integrations/aup-sms/mcp-http/tests/probe_agent_os_error_body.py`.
Il log reale baseline e' `build/cr37-sdk-baseline.log` dello stesso repository.

Da PowerShell nella root CBM, con directory privata di test gia' disponibile:

```powershell
$env:PYTHONPATH = 'C:/laragon/www/api2mcp-api/runtime'
$env:AGNO_TELEMETRY = 'false'
$env:CBM_HTTP_TEST_ROOT = "$env:LOCALAPPDATA/CBM-CR37-tests"
& C:/laragon/www/api2mcp-api/runtime/.venv/Scripts/python.exe `
  integrations/aup-sms/mcp-http/tests/probe_agent_os_error_body.py `
  build/cr36-final/codebase-memory-mcp.exe
```

La prova avvia e arresta il proprio processo CBM e conserva le fixture private.
Nessun token viene stampato. La copia canonica di questa richiesta viene
depositata in `api2mcp-api/docs/change-requests/` senza modificare codice o
richieste preesistenti. Stato PROPOSED; review, implementazione e release sono
del team destinatario. Nessun commit/push o aggiornamento runtime implicito.
