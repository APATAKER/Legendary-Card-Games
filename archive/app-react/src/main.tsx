import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import './index.css';
import App from './App.tsx';
import TabletopApp from './table/TabletopApp.tsx';

const useClassic = new URLSearchParams(window.location.search).has('classic');

createRoot(document.getElementById('root')!).render(
  <StrictMode>{useClassic ? <App /> : <TabletopApp />}</StrictMode>,
);
