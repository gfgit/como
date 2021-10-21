module.exports = {
    parserPreset: 'conventional-changelog-conventionalcommits',
    rules: {
        'body-leading-blank': [2, 'always'],
        'body-max-line-length': [2, 'always', 90],
        'footer-leading-blank': [2, 'always'],
        'footer-max-line-length': [2, 'always', 90],
        'header-max-length': [2, 'always', 90],
        'scope-case': [2, 'always', 'lower-case'],
        'scope-enum': [
            2,
            'always',
            [
                'debug',
                'deco',
                'effect',
                'input',
                'hw',
                'qpa',
                'scene',
                'script',
                'space',
                'xwl'
            ]
        ],
        'subject-case': [
            2,
            'never',
            ['sentence-case', 'start-case', 'pascal-case', 'upper-case']
        ],
        'subject-empty': [2, 'never'],
        'subject-full-stop': [2, 'never', '.'],
        'type-case': [2, 'always', 'lower-case'],
        'type-empty': [2, 'never'],
        'type-enum': [
            2,
            'always',
            [
                'build',
                'ci',
                'docs',
                'feat',
                'fix',
                'perf',
                'refactor',
                'revert',
                'style',
                'test'
            ]
        ]
    }
};
