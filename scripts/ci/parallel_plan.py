"""Plan independently validated components and their real link prerequisites."""


def plan_components(config, selected):
    components = config['components']
    needed = set()

    def visit(name, active):
        if name not in components:
            raise ValueError(f'Unknown component: {name}')
        if name in active:
            raise ValueError(f'Build prerequisite cycle: {name}')
        if not components[name].get('build_targets') or name in needed:
            return
        for parent in components[name].get('build_requires', []):
            visit(parent, active | {name})
        needed.add(name)

    for name in sorted(selected):
        visit(name, set())

    def ancestors(name):
        result = set(components[name].get('build_requires', []))
        for parent in tuple(result):
            result.update(ancestors(parent))
        return sorted(result)

    matrices, vendors, prerequisites = {}, {}, {}
    for name in sorted(needed):
        matrices[name] = {'include': [
            {'components': [name], 'configure': profile['configure'],
             'configuration': profile['configuration']}
            for profile in config['profiles'] if name not in profile.get('exclude_components', [])
        ]}
        prerequisites[name] = ancestors(name)
        vendors[name] = sorted(set(components[name].get('vendors', [])))
    return {'components': sorted(needed), 'matrices': matrices, 'vendors': vendors,
            'prerequisites': prerequisites,
            'groups': sorted({group for groups in vendors.values() for group in groups})}
