# A list to hold the targets defined in this file
# Used by the alias in the root meson.build later
subdir_ups_targets = []

# A loop to generate all desired patch files for variant builds
foreach p : patches_to_generate
    modified_target = p[0]
    base_target = p[1]
    suffix = p[2]

    ups_target = custom_target(
        'create ' + name + '-' + suffix + '.ups',
        # The output is the .ups patch file
        output : name + '-' + suffix + '.ups',
        # The inputs are the base ROM and the modified ROM, in that order
        input : [base_target, modified_target],
        # Build the command using placeholders
        command : [
            ups_cmd,
            'diff',
            '--base', '@INPUT0@', # This will be the base_target file
            '--modified', '@INPUT1@', # This will be the modified_target file
            '--output', '@OUTPUT@',
        ],
        build_by_default : false,
    )

    subdir_ups_targets += ups_target
endforeach

# Create an alias for all targets defined in this file
alias_target('patches', subdir_ups_targets)
