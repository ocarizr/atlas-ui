import pytest

from scripts.ci.owned_products import validate_products, write_product


def test_same_run_matching_profile_can_reuse_its_library(tmp_path):
    write_product(tmp_path, 'jobs', 'windows-msvc', 'Debug', 'abc', '100')
    validate_products(tmp_path, ['jobs'], 'windows-msvc', 'Debug', 'abc', '100')


@pytest.mark.parametrize('field,value', [('configure', 'windows-asan'), ('configuration', 'Release'),
                                       ('revision', 'def'), ('run', '101')])
def test_cannot_mix_sources_profiles_or_workflow_runs(tmp_path, field, value):
    write_product(tmp_path, 'jobs', 'windows-msvc', 'Debug', 'abc', '100')
    expected = dict(configure='windows-msvc', configuration='Debug', revision='abc', run='100')
    expected[field] = value
    with pytest.raises(ValueError, match='Incompatible compiled component: jobs'):
        validate_products(tmp_path, ['jobs'], **expected)


def test_missing_required_library_is_not_silently_rebuilt(tmp_path):
    with pytest.raises(ValueError, match='Missing compiled component: jobs'):
        validate_products(tmp_path, ['jobs'], 'windows-msvc', 'Debug', 'abc', '100')
