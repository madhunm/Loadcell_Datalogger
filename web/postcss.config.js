export default {
  plugins: {
    autoprefixer: {},
    cssnano: {
      preset: ['default', {
        discardComments: {
          removeAll: true,
        },
        normalizeWhitespace: true,
        minifyFontValues: true,
        minifyGradients: true,
        minifyParams: true,
        minifySelectors: true,
        mergeLonghand: true,
        mergeRules: true,
        reduceInitial: true,
        reduceTransforms: true,
      }],
    },
  },
};

