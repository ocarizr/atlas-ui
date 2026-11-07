"""Recover transient artifact-discovery failures without hiding permanent API errors."""

import io
from http.client import IncompleteRead, RemoteDisconnected
from urllib.error import HTTPError, URLError

import pytest

from scripts.ci import vendor_artifacts


@pytest.fixture
def requests(monkeypatch):
    """Capture authenticated requests and retry delays without network access or sleeping."""
    monkeypatch.setenv("GH_TOKEN", "test-token")
    calls, delays, outcomes = [], [], []

    def open_response(request, timeout):
        """Return the next isolated response or raise its modeled transport error."""
        calls.append((request, timeout))
        outcome = outcomes.pop(0)
        if isinstance(outcome, Exception):
            raise outcome
        if hasattr(outcome, "read"):
            return outcome
        return io.StringIO(outcome)

    monkeypatch.setattr(vendor_artifacts, "urlopen", open_response)
    monkeypatch.setattr(vendor_artifacts, "sleep", delays.append, raising=False)
    return calls, delays, outcomes


@pytest.mark.parametrize("status", [429, 500, 502, 503, 504], ids=lambda code: f"http-{code}")
def test_transient_http_error_retries_same_authenticated_request(requests, status):
    """A transient lookup failure can recover without changing the requested artifact identity."""
    calls, delays, outcomes = requests
    outcomes.extend([HTTPError("https://api.github.com/test", status, "temporary", {}, None), "{}"])
    assert vendor_artifacts.github_fetch("/test") == {}
    assert delays == [1]
    assert len(calls) == 2
    for request, timeout in calls:
        assert request.full_url == "https://api.github.com/test"
        assert request.get_header("Authorization") == "Bearer test-token"
        assert timeout == 60


@pytest.mark.parametrize(
    "error",
    [
        URLError("connection reset"),
        TimeoutError("timed out"),
        RemoteDisconnected("remote closed"),
        ConnectionResetError("read reset"),
    ],
    ids=["url-error", "timeout", "remote-disconnect", "read-reset"],
)
def test_transport_failure_recovers(requests, error):
    """Temporary transport failures follow the same bounded retry policy."""
    calls, delays, outcomes = requests
    outcomes.extend([error, '{"artifacts": []}'])
    assert vendor_artifacts.github_fetch("/test") == {"artifacts": []}
    assert len(calls) == 2
    assert delays == [1]


@pytest.mark.parametrize("recover", [True, False], ids=["recovers", "exhausts"])
def test_incomplete_response_is_retried_and_closed(requests, recover):
    """A truncated response body retries the whole read and never becomes a cache miss."""
    calls, delays, outcomes = requests
    error = IncompleteRead(b'{"artifacts":', 10)

    class interrupted_response(io.BytesIO):
        """Model disconnection during JSON body consumption after a successful HTTP open."""

        def read(self, *args, **kwargs):
            """Discard the incomplete body rather than parsing a partial trusted catalog."""
            raise error

    responses = [interrupted_response() for _ in range(1 if recover else 4)]
    outcomes.extend(responses)
    if recover:
        outcomes.append('{"artifacts": []}')
        assert vendor_artifacts.github_fetch("/test") == {"artifacts": []}
    else:
        with pytest.raises(IncompleteRead) as caught:
            vendor_artifacts.github_fetch("/test")
        assert caught.value is error
    assert all(response.closed for response in responses)
    assert len(calls) == (2 if recover else 4)
    assert delays == ([1] if recover else [1, 2, 4])


def test_transient_failure_exhaustion_preserves_original_error(requests):
    """Persistent service failures stop after four attempts instead of becoming a cache miss."""
    calls, delays, outcomes = requests
    error = HTTPError("https://api.github.com/test", 502, "unavailable", {}, None)
    outcomes.extend([error] * 4)
    with pytest.raises(HTTPError) as caught:
        vendor_artifacts.github_fetch("/test")
    assert caught.value is error
    assert len(calls) == 4
    assert delays == [1, 2, 4]


@pytest.mark.parametrize("status", [400, 401, 403, 404], ids=lambda code: f"http-{code}")
def test_permanent_http_error_is_not_retried(requests, status):
    """Invalid requests, permissions and missing resources fail immediately."""
    calls, delays, outcomes = requests
    outcomes.append(HTTPError("https://api.github.com/test", status, "permanent", {}, None))
    with pytest.raises(HTTPError, match=str(status)):
        vendor_artifacts.github_fetch("/test")
    assert len(calls) == 1
    assert delays == []
