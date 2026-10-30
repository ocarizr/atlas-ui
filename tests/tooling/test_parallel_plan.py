import pytest

from scripts.ci.config import read_yaml
from scripts.ci.parallel_plan import plan_components


@pytest.fixture
def configuration():
    return read_yaml('.ci/components.yaml')


def test_jobs_producer_needs_testing_but_not_grpc(configuration):
    plan = plan_components(configuration, {'jobs'})
    assert plan['vendors']['jobs'] == ['testing']
    assert plan['prerequisites']['jobs'] == []


def test_editor_requires_link_inputs_without_selecting_game(configuration):
    plan = plan_components(configuration, {'editor'})
    assert set(plan['components']) == {'editor', 'application', 'ipc', 'jobs'}
    assert 'game' not in plan['components']
    assert plan['prerequisites']['editor'] == ['application', 'ipc', 'jobs']


def test_each_matrix_row_validates_one_component(configuration):
    plan = plan_components(configuration, {'jobs', 'streaming'})
    for name, matrix in plan['matrices'].items():
        assert all(row['components'] == [name] for row in matrix['include'])


def test_native_component_omits_portable_profile(configuration):
    plan = plan_components(configuration, {'windows'})
    assert all(row['configure'] != 'windows-portable'
               for row in plan['matrices']['windows']['include'])


def test_documentation_only_needs_no_compilation(configuration):
    plan = plan_components(configuration, set())
    assert plan['components'] == []
    assert plan['groups'] == []


def test_unknown_component_fails_before_planning(configuration):
    with pytest.raises(ValueError, match='Unknown component'):
        plan_components(configuration, {'missing'})
