import { defineConfig } from 'vite';
import { resolve } from 'node:path';
import viteCompression from 'vite-plugin-compression';

export default defineConfig({
  root: '.',
  base: './',
  
  plugins: [
    // Generate gzip files for ESP32 flash savings (keep original for dev)
    viteCompression({
      algorithm: 'gzip',
      ext: '.gz',
      threshold: 0, // Compress all files
      deleteOriginFile: true, // Only keep .gz files to save flash
    }),
  ],
  
  build: {
    // Output directly to ../data for ESP32 SPIFFS
    outDir: '../data',
    emptyOutDir: true,
    
    // Aggressive minification
    minify: 'terser',
    terserOptions: {
      compress: {
        drop_console: true,
        drop_debugger: true,
        passes: 3,
        pure_funcs: ['console.log', 'console.info', 'console.debug'],
        dead_code: true,
        unused: true,
      },
      mangle: {
        toplevel: true,
      },
      format: {
        comments: false,
      },
    },
    
    // Multi-page app configuration
    rollupOptions: {
      input: {
        main: resolve(__dirname, 'index.html'),
        admin: resolve(__dirname, 'admin.html'),
        factory: resolve(__dirname, 'factory.html'),
        user: resolve(__dirname, 'user.html'),
      },
      output: {
        // Flat structure - ESP32 SPIFFS compatible
        entryFileNames: '[name].js',
        chunkFileNames: '[name].js',
        assetFileNames: '[name].[ext]',
      },
    },
    
    // Single CSS file
    cssCodeSplit: false,
    
    // No source maps
    sourcemap: false,
    
    // Inline small assets
    assetsInlineLimit: 10240,
  },
  
  // CSS processing with PostCSS
  css: {
    postcss: './postcss.config.js',
  },
  
  // Dev server settings
  server: {
    port: 3000,
    open: true,
    // Proxy API requests to Python mock server
    proxy: {
      '/api': {
        target: 'http://localhost:8080',
        changeOrigin: true,
      },
    },
  },
});
