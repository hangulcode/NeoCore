using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Text;
using System.Threading;

namespace NeoCore.Unity
{
    public sealed class HttpRequest
    {
        private readonly object gate = new object();
        private IntPtr native;
        public string Method { get; private set; }
        public string Url { get; private set; }
        public string Body { get; private set; }
        public int TimeoutMs { get; private set; }
        public IReadOnlyList<KeyValuePair<string, string>> Headers { get; private set; }
        internal HttpRequest(IntPtr request)
        {
            native = request;
            uint length;
            IntPtr text = Native.NcHttpMethod(request, out length); Method = Result.ReadUtf8(text, length);
            text = Native.NcHttpUrl(request, out length); Url = Result.ReadUtf8(text, length);
            text = Native.NcHttpBody(request, out length); Body = Result.ReadUtf8(text, length);
            TimeoutMs = Native.NcHttpTimeout(request);
            var headers = new List<KeyValuePair<string, string>>();
            for (uint i = 0; i < Native.NcHttpHeaderCount(request); ++i)
            {
                text = Native.NcHttpHeaderName(request, i, out length);
                string key = Result.ReadUtf8(text, length);
                text = Native.NcHttpHeaderValue(request, i, out length);
                headers.Add(new KeyValuePair<string, string>(key, Result.ReadUtf8(text, length)));
            }
            Headers = headers.AsReadOnly();
        }
        public bool IsCancellationRequested
        {
            get
            {
                lock (gate)
                {
                    if (native == IntPtr.Zero) throw new ObjectDisposedException(nameof(HttpRequest));
                    return Native.NcHttpIsCancelled(native) != 0;
                }
            }
        }
        internal void Close() { lock (gate) native = IntPtr.Zero; }
    }
    public readonly struct HttpResponse
    {
        public bool Success { get; }
        public string Body { get; }
        public string Error { get; }
        public HttpResponse(bool success, string body, string error = "") { Success = success; Body = body; Error = error; }
    }
    public sealed class HttpClientTransport : IDisposable
    {
        private readonly HttpClient client = new HttpClient(new HttpClientHandler { AllowAutoRedirect = false })
        {
            Timeout = Timeout.InfiniteTimeSpan,
            MaxResponseContentBufferSize = 8 * 1024 * 1024
        };
        public HttpResponse Send(HttpRequest request)
        {
            try
            {
                var uri = new Uri(request.Url);
                if (uri.Scheme != "http" && uri.Scheme != "https") throw new ArgumentException("Unsupported HTTP scheme");
                using (var cancel = new CancellationTokenSource())
                using (var message = new HttpRequestMessage(new HttpMethod(request.Method), uri))
                {
                    if (request.IsCancellationRequested) return new HttpResponse(false, "", "HTTP request cancelled");
                    if (request.TimeoutMs < -1) throw new ArgumentException("Invalid HTTP timeout");
                    if (request.TimeoutMs >= 0) cancel.CancelAfter(Math.Max(1, request.TimeoutMs));
                    if (request.Method == "POST") message.Content = new ByteArrayContent(Encoding.UTF8.GetBytes(request.Body));
                    foreach (var header in request.Headers)
                        if (!message.Headers.TryAddWithoutValidation(header.Key, header.Value))
                            if (message.Content == null || !message.Content.Headers.TryAddWithoutValidation(header.Key, header.Value))
                                throw new ArgumentException("Invalid HTTP header");
                    var pending = client.SendAsync(message, HttpCompletionOption.ResponseContentRead, cancel.Token);
                    while (!pending.IsCompleted)
                    {
                        if (request.IsCancellationRequested) cancel.Cancel();
                        Thread.Sleep(10);
                    }
                    using (var response = pending.GetAwaiter().GetResult())
                        return new HttpResponse(true, Encoding.UTF8.GetString(response.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult()));
                }
            }
            catch (Exception error) { return new HttpResponse(false, "", error.Message); }
        }
        public void Dispose() { client.Dispose(); }
    }
}
