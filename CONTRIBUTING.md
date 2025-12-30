# Contributing to Loadcell Datalogger

Thank you for your interest in contributing to the Loadcell Datalogger project! This document provides guidelines and instructions for contributing.

## How to Contribute

### Reporting Issues

If you find a bug or have a feature request:

1. **Check existing issues** - Search GitHub issues to see if the problem is already reported
2. **Create a new issue** - Provide:
   - Clear description of the problem or feature
   - Steps to reproduce (for bugs)
   - Expected vs. actual behavior
   - System configuration (hardware, firmware version)
   - Relevant logs or debug output

### Submitting Code Changes

1. **Fork the repository**
2. **Create a feature branch**:
   ```bash
   git checkout -b feature/your-feature-name
   ```
3. **Make your changes**:
   - Follow the code style guidelines (see below)
   - Add comments for new functionality
   - Update documentation as needed
4. **Test your changes**:
   - Build and test on hardware
   - Verify no regressions
   - Test edge cases
5. **Commit your changes**:
   ```bash
   git commit -m "Add: Description of your changes"
   ```
6. **Push to your fork**:
   ```bash
   git push origin feature/your-feature-name
   ```
7. **Create a Pull Request**:
   - Provide clear description of changes
   - Reference any related issues
   - Include test results

## Code Style Guidelines

### Naming Conventions

- **Variables**: `camelCase` (e.g., `systemState`, `sampleCount`)
- **Static variables**: `s_` prefix with `camelCase` (e.g., `s_loggerState`, `s_bufferPos`)
- **Constants**: `UPPER_CASE` with underscores (e.g., `MAX_BUFFER_SIZE`, `PIN_ADC_MISO`)
- **Functions**: `camelCase` (e.g., `initPeripherals()`, `loggerStartSession()`)
- **Enums**: `UPPER_CASE` (e.g., `STATE_READY`, `LOGGER_IDLE`)
- **Types/Structs**: `PascalCase` (e.g., `LoggerConfig`, `AdcSample`)

### Code Formatting

- Use 4 spaces for indentation (no tabs)
- Maximum line length: 100 characters (soft limit)
- Use braces for all control structures (even single-line)
- Add blank lines between logical sections
- Group related includes together

### Comments

- Add file headers with purpose and author
- Document all public functions with Doxygen-style comments
- Add inline comments for complex logic
- Keep comments up-to-date with code changes

Example:
```cpp
/**
 * @brief Initialize the ADC module
 * @param pgaGain Programmable gain amplifier setting
 * @return true if initialization successful, false otherwise
 */
bool adcInit(AdcPgaGain pgaGain);
```

### Error Handling

- Return `bool` for success/failure (true = success)
- Use NeoPixel patterns for user-visible errors
- Log errors to Serial with `[ERROR]` prefix
- Provide meaningful error messages

### Memory Management

- Prefer stack allocation over heap when possible
- Use `String` sparingly (prefer `char[]` for fixed-size strings)
- Avoid dynamic allocation in time-critical paths
- Check for buffer overflows

## Testing Guidelines

### Before Submitting

1. **Build verification**:
   ```bash
   pio run
   ```

2. **Hardware testing**:
   - Test on actual ESP32-S3 hardware
   - Verify all peripherals work correctly
   - Test error conditions

3. **Code review checklist**:
   - [ ] Code compiles without warnings
   - [ ] Follows naming conventions
   - [ ] Has appropriate comments
   - [ ] Handles error cases
   - [ ] No memory leaks
   - [ ] Documentation updated

### Test Cases

When adding new features, include:
- Normal operation test
- Error condition test
- Edge case test
- Performance test (if applicable)

## Documentation

### Code Documentation

- Update function documentation when changing APIs
- Add examples for complex functions
- Document any assumptions or limitations

### User Documentation

- Update `USER_MANUAL.md` for user-facing changes
- Update `BRINGUP_TEST_CALIBRATION_PLAN.md` for hardware/procedure changes
- Update `CHANGELOG.md` for significant changes
- Update `KNOWN_ISSUES.md` if adding limitations

## Pull Request Process

1. **Ensure your PR**:
   - Has a clear title and description
   - References related issues
   - Includes test results
   - Updates documentation

2. **Review process**:
   - Maintainers will review your code
   - Address any feedback or requested changes
   - Keep PR focused (one feature/fix per PR)

3. **After approval**:
   - Maintainers will merge your PR
   - Your contribution will be included in next release

## Areas for Contribution

### High Priority

- Bug fixes and stability improvements
- Performance optimizations
- Documentation improvements
- Test coverage

### Medium Priority

- New features (discuss in issues first)
- UI/UX improvements
- Additional output formats
- Enhanced error recovery

### Low Priority

- Code refactoring
- Style improvements
- Additional examples
- Translation/localization

## Questions?

- Open an issue for questions or discussions
- Check existing documentation first
- Review code examples in the repository

## Code of Conduct

- Be respectful and constructive
- Focus on technical merit
- Accept constructive criticism
- Help others learn and improve

## License

By contributing, you agree that your contributions will be licensed under the same MIT License that covers the project.

---

Thank you for contributing to Loadcell Datalogger!

