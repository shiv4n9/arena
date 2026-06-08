import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// https://vite.dev/config/
export default defineConfig({
  // For GitHub Pages project sites the app is served from /<repo>/.
  // The deploy workflow sets VITE_BASE=/ARCTIC/; locally it stays at root.
  base: process.env.VITE_BASE || '/',
  plugins: [react()],
})
