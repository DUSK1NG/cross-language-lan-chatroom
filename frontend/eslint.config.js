import js from '@eslint/js';
import globals from 'globals';
import reactHooks from 'eslint-plugin-react-hooks';
import tseslint from 'typescript-eslint';

export default tseslint.config(
  { ignores: ['dist', 'node_modules', '*.config.*'] },
  {
    files: ['src/**/*.{ts,tsx}'],
    extends: [js.configs.recommended, ...tseslint.configs.recommended],
    languageOptions: { ecmaVersion: 2022, globals: globals.browser },
    plugins: { 'react-hooks': reactHooks },
    rules: {
      'react-hooks/rules-of-hooks': 'error',
      'react-hooks/exhaustive-deps': 'warn',
      '@typescript-eslint/no-unused-vars': ['error', { argsIgnorePattern: '^_', varsIgnorePattern: '^_' }],
      // Design tokens are the only colour source: no hex or rgb literals in components.
      'no-restricted-syntax': ['error',
        { selector: "Literal[value=/#[0-9a-fA-F]{3,8}\\b/]", message: 'Use a design token (tokens.css / Tailwind theme) instead of a hex colour.' },
        { selector: "Literal[value=/\\brgba?\\(/]", message: 'Use a design token instead of an rgb()/rgba() colour.' }]
    }
  },
  {
    files: ['src/**/*.test.{ts,tsx}', 'src/test-setup.ts'],
    rules: { 'no-restricted-syntax': 'off', '@typescript-eslint/no-explicit-any': 'off' }
  }
);
