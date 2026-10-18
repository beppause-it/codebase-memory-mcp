import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import App from '../../../../../graph-ui/src/App';
import { installUiLog } from '../../../../../graph-ui/src/app/ui-log-install';
import '../../../../../graph-ui/src/styles/tokens.css';
import '../../../../../graph-ui/src/styles/terminal.css';
import '../../../../../graph-ui/src/styles/workspace.css';
import { RepositoryChatScopeProvider } from './RepositoryChatScopeProvider';

installUiLog();
const container = document.getElementById('root');
if (!container) throw new Error('Missing #root.');
const provider = new RepositoryChatScopeProvider();
createRoot(container).render(<StrictMode><App chatScopeProvider={provider} /></StrictMode>);
