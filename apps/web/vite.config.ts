import { defineConfig } from 'vite';
import basicSsl from '@vitejs/plugin-basic-ssl';

export default defineConfig({
  plugins: process.env.AVPLAYER_WEB_HTTP === "1" ? [] : [basicSsl()],
  server: {
    host: '0.0.0.0',
    https: process.env.AVPLAYER_WEB_HTTP === "1" ? undefined : {},
    proxy: {
      '/signal': {
        target: process.env.AVPLAYER_WEB_GATEWAY_HTTP ?? 'http://127.0.0.1:50065',
        changeOrigin: true,
        ws: true,
      },
      '/control': {
        target: process.env.AVPLAYER_WEB_GATEWAY_HTTP ?? 'http://127.0.0.1:50065',
        changeOrigin: true,
      },
      '/upload': {
        target: process.env.AVPLAYER_WEB_GATEWAY_HTTP ?? 'http://127.0.0.1:50065',
        changeOrigin: true,
      },
      '/config': {
        target: process.env.AVPLAYER_WEB_GATEWAY_HTTP ?? 'http://127.0.0.1:50065',
        changeOrigin: true,
      },
      '/download': {
        target: process.env.AVPLAYER_WEB_GATEWAY_HTTP ?? 'http://127.0.0.1:50065',
        changeOrigin: true,
      },
      '/agent': {
        target: process.env.AVPLAYER_WEB_GATEWAY_HTTP ?? 'http://127.0.0.1:50065',
        changeOrigin: true,
        ws: true,
      },
    },
  },
});
